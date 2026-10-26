#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "app.h"

#define QMI8658_ADDR_LOW  0x6A
#define QMI8658_ADDR_HIGH 0x6B
#define QMI8658_WHO_REG   0x00
#define QMI8658_REV_REG   0x01
#define QMI8658_WHO_VALUE 0x05

static const char *TAG = "CS006_IMU";
static imu_hw_state_t s_state;

void imu_hw_get_state(imu_hw_state_t *out)
{
    if (out) *out = s_state;
}

esp_err_t imu_hw_probe(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.probed = true;
    strlcpy(s_state.controller, "QMI8658", sizeof(s_state.controller));
    strlcpy(s_state.error, "initializing", sizeof(s_state.error));

    esp_err_t err = board_i2c_start();
    if (err != ESP_OK) goto fail;

    uint8_t addresses[] = {QMI8658_ADDR_HIGH, QMI8658_ADDR_LOW};
    uint8_t address = 0;
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        if (board_i2c_probe(addresses[i], 40) == ESP_OK) {
            address = addresses[i];
            break;
        }
    }
    if (!address) {
        err = ESP_ERR_NOT_FOUND;
        goto fail;
    }

    uint8_t who = 0;
    uint8_t rev = 0;
    err = board_i2c_read_reg(address, QMI8658_WHO_REG, &who, 1, 50);
    if (err != ESP_OK) goto fail;
    err = board_i2c_read_reg(address, QMI8658_REV_REG, &rev, 1, 50);
    if (err != ESP_OK) goto fail;

    s_state.address = address;
    s_state.who_am_i = who;
    s_state.revision = rev;
    if (who != QMI8658_WHO_VALUE) {
        err = ESP_ERR_INVALID_RESPONSE;
        snprintf(s_state.error, sizeof(s_state.error),
                 "WHO_AM_I 0x%02X expected 0x%02X", who, QMI8658_WHO_VALUE);
        goto fail_log_only;
    }

    s_state.ready = true;
    strlcpy(s_state.error, "none", sizeof(s_state.error));
    ESP_LOGI(TAG, "QMI8658 ready addr=0x%02X WHO=0x%02X revision=0x%02X",
             address, who, rev);
    return ESP_OK;

fail:
    snprintf(s_state.error, sizeof(s_state.error), "%s (0x%x)",
             esp_err_to_name(err), (unsigned)err);
fail_log_only:
    ESP_LOGW(TAG, "IMU probe failed: %s", s_state.error);
    return err;
}
