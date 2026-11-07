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

/* Changes the colour of each glyph in -PI every few rendered frames. */
static uint16_t moving_color(unsigned t, unsigned offset)
{
    static const uint8_t palette[][3] = {
        {0, 171, 255}, {125, 68, 245}, {255, 51, 151},
        {255, 160, 42}, {24, 207, 169}, {64, 104, 250}
    };
    unsigned i = ((t / 3U) + offset) % 6U;
    return rgb(palette[i][0], palette[i][1], palette[i][2]);
}

static void page_marks(unsigned active)
{
    for (unsigned i = 0; i < PAGE_COUNT; i++)
        rect(91 + (int)i * 16, 307, 9, 3,
             i == active ? rgb(17, 49, 70) : rgb(206, 214, 222));
}

static void render_home(unsigned tick, unsigned glitch)
{
    const uint16_t paper = rgb(255,255,255);
    const uint16_t ink = rgb(22,26,32);
    const uint16_t muted = rgb(107,115,123);
    rect(0,0,LCD_W,LCD_H,paper);

    /* PRISMA-PI: all nine glyphs fit inside 240px at scale 4. */
    if (glitch && ((tick & 1U) == 0U)) {
        text(17,76,"PRISMA",4,moving_color(tick,2));
        text(161,76,"-PI",4,moving_color(tick,4));
        rect((int)((tick * 31U) % 160U),105,60,3,moving_color(tick,0));
    }
    text(14,82,"PRISMA",4,ink);
    text(158,82,"-",4,moving_color(tick,0));
    text(182,82,"P",4,moving_color(tick,2));
    text(206,82,"I",4,moving_color(tick,4));
    if (glitch && tick % 3U == 0U) {
        rect(18,111,202,2,moving_color(tick,3));
        rect(44,123,148,2,moving_color(tick,1));
    }

    centered_text(151,"CAMERA SYNTH",2,ink);
    for(int i=0;i<4;i++) rect(24+i*48,185,42,4,moving_color(tick,(unsigned)i));
    centered_text(223,"TAP GLITCH",2,muted);
    centered_text(256,"SWIPE LEFT",2,muted);
    centered_text(287,"VUDUM DEVELOPMENTS",1,muted);
    page_marks(0);
}

static void render_admin(unsigned tick)
{
    network_state_t net = {0};
    network_get_state(&net);
    uint16_t ink=rgb(24,27,35), muted=rgb(105,115,125), accent=moving_color(tick,0);
    rect(0,0,LCD_W,LCD_H,rgb(253,253,254));
    rect(0,0,LCD_W,6,accent);
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
    uint16_t ink=rgb(24,27,35), muted=rgb(105,115,125), accent=moving_color(tick,2);
    rect(0,0,LCD_W,LCD_H,rgb(253,253,254));
    rect(0,0,LCD_W,6,accent);
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
    uint16_t ink=rgb(24,27,35), muted=rgb(105,115,125), accent=moving_color(tick,4);
    rect(0,0,LCD_W,LCD_H,rgb(253,253,254));
    rect(0,0,LCD_W,6,accent);
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
            if (page == 0) glitch = 12;
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
                default: render_home(tick,glitch); break;
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

    render_home(0,0);
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
    ESP_LOGI(TAG, "Animated PRISMA-PI, GLITCH, 4 swipe pages started");
    return ESP_OK;
}
