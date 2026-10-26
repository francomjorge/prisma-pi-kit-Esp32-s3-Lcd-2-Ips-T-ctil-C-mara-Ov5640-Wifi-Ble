#include "app.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "CS005_SPI";
static bool s_ready;

bool board_spi_ready(void) { return s_ready; }

esp_err_t board_spi_start(void)
{
    if (s_ready) return ESP_OK;

    esp_err_t e = gpio_set_direction(GPIO_NUM_45, GPIO_MODE_OUTPUT);
    if (e != ESP_OK) return e;
    e = gpio_set_direction(GPIO_NUM_41, GPIO_MODE_OUTPUT);
    if (e != ESP_OK) return e;
    gpio_set_level(GPIO_NUM_45, 1); /* LCD CS idle */
    gpio_set_level(GPIO_NUM_41, 1); /* SD CS idle */

    spi_bus_config_t bus = {
        .sclk_io_num = 39,
        .mosi_io_num = 38,
        .miso_io_num = 40,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 320 * 24 * 2
    };

    e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI2 already initialized; adopting existing bus");
        e = ESP_OK;
    }
    if (e != ESP_OK) return e;

    s_ready = true;
    ESP_LOGI(TAG, "Shared SPI2 ready SCLK=39 MOSI=38 MISO=40 LCD_CS=45 SD_CS=41");
    return ESP_OK;
}
