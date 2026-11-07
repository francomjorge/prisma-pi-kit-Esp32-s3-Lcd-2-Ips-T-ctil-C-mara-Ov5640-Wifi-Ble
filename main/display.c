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
#include "ui_model.h"
#include "esp_timer.h"

#define LCD_W 240
#define LCD_H 320
#define DMA_ROWS 20
#define MARKER_Y (LCD_H - 16)
#define TOUCH_POLL_MS 25

static const char *TAG = "CS010T_LCD";
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

static esp_err_t present_region(int y0, int y1)
{
    int pending = 0;
    int seq = 0;
    for (int y = y0; y < y1; y += DMA_ROWS) {
        int rows = (y + DMA_ROWS <= y1) ? DMA_ROWS : (y1 - y);
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

static esp_err_t present_once(void)
{
    return present_region(0, LCD_H);
}

/* PRISMA-PI LITE source of truth: original black #04070C background,
 * white logo, cyan active dot, dark inactive dots and five -PI/glitch colors.
 * The 240x240 visual composition is vertically centred on the 240x320 LCD.
 * Navigation markers use all four real pages, at the LCD bottom.
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
    /* Exact prior layout (x=92,108,124,140; 8x8); aligned 16 px
     * from the 320px panel bottom rather than the old 240px panel. */
    const uint16_t cyan = rgb(35,220,220);
    const uint16_t dim = rgb(45,60,75);
    for (unsigned i = 0; i < UI_PAGE_COUNT; i++)
        rect(92 + (int)i * 16, MARKER_Y, 8, 8,
             i == active ? cyan : dim);
}

/*
 * Exact legacy algorithm from prisma-pi-lite-waveshare/main/main.c:
 * nine colourful rectangles per tap, count-dependent offsets. We do not clear
 * the scene here, so every previous tap remains visible until page navigation.
 */
static void accumulate_glitch(unsigned glitch_index, int x, int y)
{
    for (unsigned i = 0; i < 9U; i++) {
        ui_glitch_rect_t block = ui_model_glitch_rect(glitch_index,i,x,y);
        if (block.x < 0 || block.x + block.size > LCD_W ||
            block.y < REFERENCE_SCENE_Y ||
            block.y + block.size > REFERENCE_SCENE_Y + REFERENCE_SCENE_H) continue;
        rect(block.x, block.y, block.size, block.size,
             moving_color(0, block.palette_index));
    }
}

static void render_home(unsigned phase)
{
    const uint16_t bg = rgb(4,7,12);
    const uint16_t white = rgb(240,245,250);
    rect(0,0,LCD_W,LCD_H,bg);

    /* Original logo palette: -PI index (glyph + phase) % 5. */
    text(14,REFERENCE_SCENE_Y+39,"PRISMA",4,white);
    text(158,REFERENCE_SCENE_Y+39,"-",4,moving_color(phase * 6U,0));
    text(182,REFERENCE_SCENE_Y+39,"P",4,moving_color(phase * 6U,1));
    text(206,REFERENCE_SCENE_Y+39,"I",4,moving_color(phase * 6U,2));
    centered_text(REFERENCE_SCENE_Y+118,"CAMERA",3,white);
    centered_text(REFERENCE_SCENE_Y+156,"SYNTH",3,white);
    page_marks(UI_PAGE_HOME);
}

static void render_admin(unsigned tick)
{
    (void)tick;
    network_state_t net = {0};
    network_get_state(&net);
    uint16_t ink=rgb(240,245,250), muted=rgb(75,82,90), accent=rgb(35,220,220);
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
    (void)tick;
    network_state_t net = {0};
    discovery_state_t mdns = {0};
    network_get_state(&net);
    discovery_get_state(&mdns);
    uint16_t ink=rgb(240,245,250), muted=rgb(75,82,90), accent=rgb(35,220,220);
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
    (void)tick;
    storage_state_t st = {0};
    camera_hw_state_t cam = {0};
    touch_hw_state_t touch = {0};
    storage_get_state(&st);
    camera_hw_get_state(&cam);
    touch_hw_get_state(&touch);
    uint16_t ink=rgb(240,245,250), muted=rgb(75,82,90), accent=rgb(35,220,220);
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    
    centered_text(30,"SYSTEM",3,ink);
    centered_text(78,"CS010T PLATFORM",2,accent);
    centered_text(120,touch.ready?"TOUCH READY":"TOUCH ERROR",2,ink);
    centered_text(155,cam.ready?"CAMERA PROBED":"CAMERA ERROR",2,ink);
    centered_text(190,st.internal.mounted?"STORAGE READY":"STORAGE OFF",2,ink);
    centered_text(232,"ADMIN VIA WEB",2,ink);
    centered_text(276,"SWIPE HOME",1,muted);
    page_marks(3);
}

static void display_task(void *unused)
{
    ui_model_t ui;
    ui_model_init(&ui, (uint32_t)(esp_timer_get_time() / 1000ULL));
    uint32_t last_status_redraw = 0;
    bool initial = true;

    for (;;) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        int action = touch_hw_poll_action();
        ui_change_t change = ui_model_input(&ui, action, now_ms);
        esp_err_t err = ESP_OK;

        if (initial || change == UI_CHANGE_PAGE) {
            /* Entering any page explicitly clears previous glitch blocks.
             * Returning HOME restarts the logo animation. */
            initial = false;
            switch (ui.page) {
                case UI_PAGE_ADMIN:    render_admin(now_ms / 20U); break;
                case UI_PAGE_NETWORK:  render_network(now_ms / 20U); break;
                case UI_PAGE_SYSTEM:   render_system(now_ms / 20U); break;
                default:               render_home(ui.phase); break;
            }
            err = present_once();
            last_status_redraw = now_ms;
        } else if (change == UI_CHANGE_ADD_GLITCH) {
            uint16_t x = 120, y = REFERENCE_SCENE_Y + 138;
            touch_hw_last_point(&x, &y);
            /* The first tap freezes the animated -PI. Only draw added squares;
             * do not call render_home again while glitch_count > 0. */
            accumulate_glitch(ui.glitch_count - 1U, (int)x, (int)y);
            err = present_region(REFERENCE_SCENE_Y,
                                 REFERENCE_SCENE_Y + REFERENCE_SCENE_H);
            ESP_LOGI(TAG, "GLITCH %lu accumulated; palette frozen",
                     (unsigned long)ui.glitch_count);
        } else if (ui_model_animation_step(&ui, now_ms)) {
            render_home(ui.phase);
            err = present_region(REFERENCE_SCENE_Y,
                                 REFERENCE_SCENE_Y + REFERENCE_SCENE_H);
        } else if (ui.page != UI_PAGE_HOME &&
                   (uint32_t)(now_ms - last_status_redraw) >= 3000U) {
            /* Update network/system status, not the frozen HOME framebuffer. */
            if (ui.page == UI_PAGE_ADMIN) render_admin(now_ms / 20U);
            else if (ui.page == UI_PAGE_NETWORK) render_network(now_ms / 20U);
            else render_system(now_ms / 20U);
            err = present_once();
            last_status_redraw = now_ms;
        }

        if (err != ESP_OK)
            ESP_LOGE(TAG, "LCD draw failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(20));
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

    render_home(0);
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
    ESP_LOGI(TAG, "Legacy PRISMA-PI: palette pause + accumulated glitch, swipe-left ADMIN");
    return ESP_OK;
}
