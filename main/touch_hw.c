#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "app.h"

#define TOUCH_ADDR 0x15
#define TOUCH_CHIP_ID_REG 0xA7

static const char *TAG = "CS006_TOUCH";
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
    strlcpy(s_state.controller, "CST816D", sizeof(s_state.controller));
    strlcpy(s_state.error, "initializing", sizeof(s_state.error));

    esp_err_t err = board_i2c_start();
    if (err != ESP_OK) goto fail;
    err = board_i2c_probe(TOUCH_ADDR, 50);
    if (err != ESP_OK) goto fail;

    uint8_t id = 0;
    err = board_i2c_read_reg(TOUCH_ADDR, TOUCH_CHIP_ID_REG, &id, 1, 50);
    if (err != ESP_OK) goto fail;

    s_state.chip_id = id;
    s_state.ready = true;
    snprintf(s_state.controller, sizeof(s_state.controller), "CST816D ID 0x%02X", id);
    strlcpy(s_state.error, "none", sizeof(s_state.error));
    ESP_LOGI(TAG, "Touch probe OK addr=0x%02X chip_id=0x%02X", TOUCH_ADDR, id);
    return ESP_OK;

fail:
    snprintf(s_state.error, sizeof(s_state.error), "%s (0x%x)",
             esp_err_to_name(err), (unsigned)err);
    ESP_LOGW(TAG, "Touch probe failed: %s", s_state.error);
    return err;
}
