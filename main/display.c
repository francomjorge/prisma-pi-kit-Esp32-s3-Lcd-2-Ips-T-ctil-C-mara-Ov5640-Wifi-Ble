#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app.h"

#define LCD_W 240
#define LCD_H 320
#define DMA_ROWS 20
#define PAGE_COUNT 4
#define TOUCH_POLL_MS 25

static const char *TAG = "CS010R_LCD";
static uint16_t *frame;
static uint16_t *stripe[2];
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t dma_done;
static bool s_ready;

bool display_ready(void) { return s_ready; }

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 248U) << 8) | ((g & 252U) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

static void rect(int x, int y, int w, int h, uint16_t c)
{
    for (int yy = y; yy < y + h && yy < LCD_H; ++yy) {
        if (yy < 0) continue;
        for (int xx = x; xx < x + w && xx < LCD_W; ++xx) {
            if (xx >= 0) frame[(size_t)yy * LCD_W + xx] = c;
        }
    }
}

#include "font.inc"

static void centered_text(int y, const char *s, int scale, uint16_t color)
{
    int width = (int)strlen(s) * 6 * scale - scale;
    text((LCD_W - width) / 2, y, s, scale, color);
}

static bool color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(dma_done, &wake);
    return wake == pdTRUE;
}

static esp_err_t wait_dma(void)
{
    return xSemaphoreTake(dma_done, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t present_once(void)
{
    int pending = 0;
    int seq = 0;
    for (int y = 0; y < LCD_H; y += DMA_ROWS) {
        int rows = (y + DMA_ROWS <= LCD_H) ? DMA_ROWS : (LCD_H - y);
        if (pending == 2) {
            esp_err_t e = wait_dma();
            if (e != ESP_OK) return e;
            --pending;
        }
        int slot = seq & 1;
        memcpy(stripe[slot], frame + (size_t)y * LCD_W, (size_t)LCD_W * rows * 2U);
        esp_err_t e = esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_W, y + rows, stripe[slot]);
        if (e != ESP_OK) return e;
        ++pending;
        ++seq;
    }
    while (pending > 0) {
        esp_err_t e = wait_dma();
        if (e != ESP_OK) return e;
        --pending;
    }
    return ESP_OK;
}

/* Reference appearance: PRISMA-PI SIMPLE SYNTH (Build 58).
 * Camera appliance retains the exact visual grammar, not the old PCB pins:
 * 240x240 saturated blue stage, black letterbox on the taller 240x320 LCD,
 * white PRISMA, animated multicolour -PI, tap-triggered square glitch.
 */
#define REFERENCE_SCENE_Y 40
#define REFERENCE_SCENE_H 240

static uint16_t moving_color(unsigned tick, unsigned offset)
{
    static const uint8_t palette[][3] = {
        {30,120,255}, {175,55,255}, {255,35,150},
        {255,120,25}, {45,225,120}
    };
    unsigned i = ((tick / 6U) + offset) % 5U;
    return rgb(palette[i][0], palette[i][1], palette[i][2]);
}

static void page_marks(unsigned active)
{
    for (unsigned i = 0; i < PAGE_COUNT; i++)
        rect(86 + (int)i * 19, 307, 10, 4,
             i == active ? rgb(35,220,220) : rgb(40,63,77));
}

static void glitch_pixels(unsigned tick, int origin_x, int origin_y)
{
    /* Deterministic animated burst of 32 independent pixel blocks; no lines. */
    for (int i = 0; i < 32; ++i) {
        unsigned seed = (unsigned)i * 7919U + tick * 113U;
        int dx = (int)((seed * 37U + 17U) % 139U) - 69;
        int dy = (int)((seed * 53U + 31U) % 143U) - 71;
        int x = origin_x + dx;
        int y = origin_y + dy;
        int size = 4 + (int)((seed >> 4) % 4U) * 3;
        if (x < 1 || x + size >= LCD_W ||
            y < REFERENCE_SCENE_Y + 3 ||
            y + size > REFERENCE_SCENE_Y + REFERENCE_SCENE_H - 4) continue;
        rect(x,y,size,size,moving_color(tick,(unsigned)i));
    }
}

static void render_home(unsigned tick, unsigned glitch, int tap_x, int tap_y)
{
    const uint16_t surround = rgb(4,7,12);
    const uint16_t vivid_blue = rgb(0,119,228);
    const uint16_t white = rgb(240,245,250);
    const uint16_t cyan = rgb(30,235,245);
    rect(0,0,LCD_W,LCD_H,surround);
    rect(0,REFERENCE_SCENE_Y,LCD_W,REFERENCE_SCENE_H,vivid_blue);

    /* The reference logo was precisely 212px wide at font scale 4. */
    text(14,REFERENCE_SCENE_Y+39,"PRISMA",4,white);
    text(158,REFERENCE_SCENE_Y+39,"-",4,moving_color(tick,2));
    text(182,REFERENCE_SCENE_Y+39,"P",4,moving_color(tick,3));
    text(206,REFERENCE_SCENE_Y+39,"I",4,moving_color(tick,4));

    centered_text(REFERENCE_SCENE_Y+118,"CAMERA",3,white);
    centered_text(REFERENCE_SCENE_Y+156,"SYNTH",3,white);

    /* Cyan page indicators of the original Simple Synth design. */
    for (int i=0; i<8; ++i)
        rect(58+i*17,REFERENCE_SCENE_Y+222,10,8,cyan);

    if (glitch) {
        if (tap_x < 20 || tap_x > 220) tap_x = 120;
        if (tap_y < REFERENCE_SCENE_Y+85 || tap_y > REFERENCE_SCENE_Y+195)
            tap_y = REFERENCE_SCENE_Y+138;
        glitch_pixels(tick,tap_x,tap_y);
    }
}

static void render_admin(unsigned tick)
{
    network_state_t net = {0};
    network_get_state(&net);
    uint16_t ink=rgb(240,245,250), muted=rgb(100,131,149), accent=moving_color(tick,0);
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    
    centered_text(33,"ADMIN",4,ink);
    centered_text(85,"WEB CONTROL",2,accent);
    centered_text(125,"CONNECT TO AP",2,ink);
    centered_text(158,net.ip[0]?net.ip:"192.168.4.1",2,ink);
    centered_text(204,"OPEN WEB ADMIN",2,ink);
    centered_text(245,"SWIPE TO NETWORK",1,muted);
    page_marks(1);
}

static void render_network(unsigned tick)
{
    network_state_t net = {0};
    discovery_state_t mdns = {0};
    network_get_state(&net);
    discovery_get_state(&mdns);
    uint16_t ink=rgb(240,245,250), muted=rgb(100,131,149), accent=moving_color(tick,2);
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    
    centered_text(30,"NETWORK",3,ink);
    centered_text(78,"AP ADDRESS",2,accent);
    centered_text(110,net.ip[0]?net.ip:"UNAVAILABLE",2,ink);
    centered_text(157,"STA ADDRESS",2,accent);
    centered_text(188,net.sta_connected?net.sta_ip:"DISCONNECTED",1,ink);
    centered_text(230,mdns.ready?"MDNS READY":"MDNS OFF",2,ink);
    centered_text(276,"SWIPE FOR SYSTEM",1,muted);
    page_marks(2);
}

static void render_system(unsigned tick)
{
    storage_state_t st = {0};
    camera_hw_state_t cam = {0};
    touch_hw_state_t touch = {0};
    storage_get_state(&st);
    camera_hw_get_state(&cam);
    touch_hw_get_state(&touch);
    uint16_t ink=rgb(240,245,250), muted=rgb(100,131,149), accent=moving_color(tick,4);
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    
    centered_text(30,"SYSTEM",3,ink);
    centered_text(78,"CS010R PLATFORM",2,accent);
    centered_text(120,touch.ready?"TOUCH READY":"TOUCH ERROR",2,ink);
    centered_text(155,cam.ready?"CAMERA PROBED":"CAMERA ERROR",2,ink);
    centered_text(190,st.internal.mounted?"STORAGE READY":"STORAGE OFF",2,ink);
    centered_text(232,"ADMIN VIA WEB",2,ink);
    centered_text(276,"SWIPE HOME",1,muted);
    page_marks(3);
}

static void display_task(void *unused)
{
    unsigned tick = 0, page = 0, glitch = 0;
    int glitch_x = 120, glitch_y = REFERENCE_SCENE_Y + 138;
    unsigned last_page = PAGE_COUNT;
    for (;;) {
        int action = touch_hw_poll_action();
        if (action == 2) {
            page = (page + 1U) % PAGE_COUNT;
            glitch = 0;
        } else if (action == 3) {
            page = (page + PAGE_COUNT - 1U) % PAGE_COUNT;
            glitch = 0;
        } else if (action == 1) {
            if (page == 0) {
                uint16_t x = 120, y = REFERENCE_SCENE_Y + 138;
                touch_hw_last_point(&x,&y);
                glitch_x = (int)x;
                glitch_y = (int)y;
                glitch = 12;
            }
            else page = 0;
        }
        /* Do not repaint 150 KiB continuously while Wi-Fi clients connect.
         * Keep frequent touch polling, draw only when a page changes, in a
         * brief glitch, or periodically for changing status and logo. */
        bool redraw = (page != last_page);
        if (page == 0 && ((tick % 8U) == 0U || glitch)) redraw = true;
        if (page != 0 && ((tick % 80U) == 0U)) redraw = true;
        if (redraw) {
            last_page = page;
            switch (page) {
                case 1: render_admin(tick); break;
                case 2: render_network(tick); break;
                case 3: render_system(tick); break;
                default: render_home(tick,glitch,glitch_x,glitch_y); break;
            }
            esp_err_t e = present_once();
            if (e != ESP_OK) {
                ESP_LOGE(TAG,"LCD draw failed: %s",esp_err_to_name(e));
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            if (glitch) glitch--;
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

esp_err_t display_start(void)
{
    s_ready = false;
    frame = heap_caps_malloc(LCD_W * LCD_H * 2U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frame) frame = heap_caps_malloc(LCD_W * LCD_H * 2U, MALLOC_CAP_8BIT);
    stripe[0] = heap_caps_malloc(LCD_W * DMA_ROWS * 2U, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    stripe[1] = heap_caps_malloc(LCD_W * DMA_ROWS * 2U, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    dma_done = xSemaphoreCreateCounting(2, 0);
    if (!frame || !stripe[0] || !stripe[1] || !dma_done) return ESP_ERR_NO_MEM;

    esp_err_t e = gpio_set_direction(GPIO_NUM_1, GPIO_MODE_OUTPUT);
    if (e != ESP_OK) return e;
    gpio_set_level(GPIO_NUM_1, 0);

    e = board_spi_start();
    if (e != ESP_OK) return e;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = 42, .cs_gpio_num = 45, .pclk_hz = 40000000,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0,
        .trans_queue_depth = 2, .on_color_trans_done = color_done
    };
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io);
    if (e != ESP_OK) return e;

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16
    };
    e = esp_lcd_new_panel_st7789(io, &panel_cfg, &panel);
    if (e != ESP_OK) return e;
    if ((e = esp_lcd_panel_reset(panel)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_init(panel)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_mirror(panel, false, false)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_swap_xy(panel, false)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_invert_color(panel, true)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_disp_on_off(panel, true)) != ESP_OK) return e;

    render_home(0,0,120,REFERENCE_SCENE_Y+138);
    e = present_once();
    if (e != ESP_OK) return e;
    gpio_set_level(GPIO_NUM_1, 1);
    s_ready = true;

    /* Keep framebuffer/DMA stripes for actual animation and swipe navigation. */
    if (xTaskCreatePinnedToCore(display_task, "prisma_display", 4096, NULL,
                                3, NULL, 1) != pdPASS) {
        s_ready = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Reference PRISMA-PI blue UI, tap pixel glitch and swipe navigation started");
    return ESP_OK;
}
