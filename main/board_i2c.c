#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "app.h"

#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_SDA GPIO_NUM_48
#define BOARD_I2C_SCL GPIO_NUM_47
#define BOARD_I2C_HZ 400000

static const char *TAG = "CS006_I2C";
static bool s_ready;
static SemaphoreHandle_t s_mutex;

bool board_i2c_ready(void) { return s_ready; }

esp_err_t board_i2c_start(void)
{
    if (s_ready) return ESP_OK;
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_HZ
    };
    esp_err_t e = i2c_param_config(BOARD_I2C_PORT, &cfg);
    if (e != ESP_OK) return e;
    e = i2c_driver_install(BOARD_I2C_PORT, cfg.mode, 0, 0, 0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    s_ready = true;
    ESP_LOGI(TAG, "Shared I2C0 ready SDA=48 SCL=47 clock=%u", BOARD_I2C_HZ);
    return ESP_OK;
}

static bool lock_bus(uint32_t timeout_ms)
{
    return s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void unlock_bus(void)
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

esp_err_t board_i2c_probe(uint8_t address, uint32_t timeout_ms)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!lock_bus(timeout_ms)) return ESP_ERR_TIMEOUT;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) { unlock_bus(); return ESP_ERR_NO_MEM; }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t e = i2c_master_cmd_begin(BOARD_I2C_PORT, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);
    unlock_bus();
    return e;
}

esp_err_t board_i2c_read_reg(uint8_t address, uint8_t reg, uint8_t *data,
                             size_t len, uint32_t timeout_ms)
{
    if (!s_ready || !data || len == 0) return ESP_ERR_INVALID_ARG;
    if (!lock_bus(timeout_ms)) return ESP_ERR_TIMEOUT;
    esp_err_t e = i2c_master_write_read_device(
        BOARD_I2C_PORT, address, &reg, 1, data, len, pdMS_TO_TICKS(timeout_ms));
    unlock_bus();
    return e;
}

esp_err_t board_i2c_write_reg(uint8_t address, uint8_t reg, const uint8_t *data,
                              size_t len, uint32_t timeout_ms)
{
    if (!s_ready || (!data && len)) return ESP_ERR_INVALID_ARG;
    if (len > 31) return ESP_ERR_INVALID_SIZE;
    uint8_t buffer[32];
    buffer[0] = reg;
    if (len) memcpy(buffer + 1, data, len);
    if (!lock_bus(timeout_ms)) return ESP_ERR_TIMEOUT;
    esp_err_t e = i2c_master_write_to_device(
        BOARD_I2C_PORT, address, buffer, len + 1, pdMS_TO_TICKS(timeout_ms));
    unlock_bus();
    return e;
}
