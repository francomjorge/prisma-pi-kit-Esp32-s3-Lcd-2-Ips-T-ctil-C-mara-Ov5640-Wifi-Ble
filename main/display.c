#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
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

static const char *TAG = "CS001_LCD";
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
            if (xx >= 0) frame[yy * LCD_W + xx] = c;
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

static void draw_product_screen(void)
{
    const uint16_t paper = rgb(255, 255, 255);
    const uint16_t ink = rgb(18, 20, 24);
    const uint16_t muted = rgb(111, 116, 124);
    const uint16_t cyan = rgb(0, 168, 255);
    const uint16_t magenta = rgb(255, 45, 141);
    const uint16_t amber = rgb(255, 181, 71);
    const uint16_t violet = rgb(119, 92, 255);
    const uint16_t green = rgb(28, 156, 101);

    rect(0, 0, LCD_W, LCD_H, paper);

    text(20, 64, "PRISMA", 4, ink);
    text(164, 64, "-", 4, magenta);
    text(188, 64, "P", 4, cyan);

    centered_text(116, "CAMERA", 3, ink);

    rect(48, 156, 36, 3, cyan);
    rect(84, 156, 36, 3, magenta);
    rect(120, 156, 36, 3, amber);
    rect(156, 156, 36, 3, violet);

    centered_text(184, "CAMERA SYNC", 2, ink);
    centered_text(232, "READY", 2, green);
    centered_text(288, "VUDUM DEVELOPMENTS", 1, muted);
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

    spi_bus_config_t bus = {
        .sclk_io_num = 39,
        .mosi_io_num = 38,
        .miso_io_num = 40,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_W * DMA_ROWS * 2
    };
    e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) return e;

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = 42,
        .cs_gpio_num = 45,
        .pclk_hz = 40000000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 2,
        .on_color_trans_done = color_done
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

    draw_product_screen();
    e = present_once();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "Static presentation failed: %s", esp_err_to_name(e));
        return e;
    }

    gpio_set_level(GPIO_NUM_1, 1);
    s_ready = true;
    ESP_LOGI(TAG, "Static 240x320 product screen ready; no refresh task started");

    free(frame); frame = NULL;
    free(stripe[0]); stripe[0] = NULL;
    free(stripe[1]); stripe[1] = NULL;
    vSemaphoreDelete(dma_done); dma_done = NULL;
    return ESP_OK;
}
