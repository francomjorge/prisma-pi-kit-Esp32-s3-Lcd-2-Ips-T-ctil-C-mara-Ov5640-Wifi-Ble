#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "app.h"

#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_ADDR 0x15
#define TOUCH_CHIP_ID_REG 0xA7

static const char *TAG = "CS003_TOUCH";
static touch_hw_state_t s_state;

void touch_hw_get_state(touch_hw_state_t *out)
{
    if (out) *out = s_state;
}

esp_err_t touch_hw_probe(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.probed = true;
    s_state.address = TOUCH_ADDR;
    strlcpy(s_state.controller, "CST816-family", sizeof(s_state.controller));
    strlcpy(s_state.error, "initializing", sizeof(s_state.error));

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 48,
        .scl_io_num = 47,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000
    };

    esp_err_t err = i2c_param_config(TOUCH_I2C_PORT, &cfg);
    if (err != ESP_OK) goto fail;
    err = i2c_driver_install(TOUCH_I2C_PORT, cfg.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;

    uint8_t reg = TOUCH_CHIP_ID_REG;
    uint8_t id = 0;
    err = i2c_master_write_read_device(TOUCH_I2C_PORT, TOUCH_ADDR, &reg, 1, &id, 1, pdMS_TO_TICKS(50));
    if (err != ESP_OK) goto fail;

    s_state.chip_id = id;
    s_state.ready = true;
    snprintf(s_state.controller, sizeof(s_state.controller), "CST816 ID 0x%02X", id);
    strlcpy(s_state.error, "none", sizeof(s_state.error));
    ESP_LOGI(TAG, "Touch probe OK addr=0x%02X chip_id=0x%02X", TOUCH_ADDR, id);
    return ESP_OK;

fail:
    snprintf(s_state.error, sizeof(s_state.error), "%s (0x%x)", esp_err_to_name(err), (unsigned)err);
    ESP_LOGW(TAG, "Touch probe failed: %s", s_state.error);
    return err;
}
