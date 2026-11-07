#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"

/* CST816D on the shared I2C bus; reads from the published 0x01-0x06 map. */
#define TOUCH_ADDR 0x15
#define TOUCH_CHIP_ID_REG 0xA7
#define GESTURE_REG 0x01
#define TOUCH_LOCKOUT_MS 320U

static const char *TAG = "CS010R_TOUCH";
static touch_hw_state_t s_state;
static bool s_pressed;
static uint16_t s_x0, s_y0, s_x_last, s_y_last;
static uint8_t s_samples;
static uint64_t s_press_ms, s_event_ms;

void touch_hw_get_state(touch_hw_state_t *out)
{
    if (out) *out = s_state;
}

esp_err_t touch_hw_probe(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_pressed = false;
    s_samples = 0;
    s_event_ms = 0;
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
    ESP_LOGI(TAG, "Touch ready: 0x%02X id=0x%02X; polling gestures", TOUCH_ADDR, id);
    return ESP_OK;

fail:
    snprintf(s_state.error, sizeof(s_state.error), "%s (0x%x)",
             esp_err_to_name(err), (unsigned)err);
    ESP_LOGW(TAG, "Touch probe failed: %s", s_state.error);
    return err;
}

/*
 * A tap must have two real contact samples and a clean release.
 * A swipe is resolved on release, then all events are locked out briefly.
 * This avoids the formerly observed autonomous tabs and tap-after-swipe.
 */
int touch_hw_poll_action(void)
{
    if (!s_state.ready) return 0;
    uint8_t b[6] = {0};
    if (board_i2c_read_reg(TOUCH_ADDR, GESTURE_REG, b, sizeof(b), 30) != ESP_OK)
        return 0;

    uint64_t now = (uint64_t)esp_timer_get_time() / 1000ULL;
    bool finger = b[1] == 1;
    uint16_t x = (uint16_t)(((b[2] & 0x0f) << 8) | b[3]);
    uint16_t y = (uint16_t)(((b[4] & 0x0f) << 8) | b[5]);
    uint8_t gesture = b[0];

    if (finger) {
        if (!s_pressed) {
            s_pressed = true;
            s_press_ms = now;
            s_x0 = s_x_last = x;
            s_y0 = s_y_last = y;
            s_samples = 1;
        } else {
            s_x_last = x;
            s_y_last = y;
            if (s_samples < 255) s_samples++;
        }
        return 0;
    }
    if (!s_pressed) return 0;
    s_pressed = false;

    uint64_t duration = now - s_press_ms;
    int dx = (int)s_x_last - (int)s_x0;
    int dy = (int)s_y_last - (int)s_y0;
    if (duration > 1200 || s_samples < 2 ||
        (s_event_ms && now - s_event_ms < TOUCH_LOCKOUT_MS))
        return 0;

    int event = 0;
    /* Require an actual horizontal displacement, regardless of noisy gesture IDs. */
    if (abs(dx) >= 42 && abs(dx) > abs(dy)) {
        event = dx < 0 ? 2 : 3;
    } else if (duration >= 65 && duration <= 650 &&
               abs(dx) <= 18 && abs(dy) <= 18 &&
               gesture != 1 && gesture != 2 &&
               gesture != 3 && gesture != 4) {
        event = 1;
    }
    if (event) {
        s_event_ms = now;
        ESP_LOGI(TAG, "event=%d dx=%d dy=%d duration=%llu ms",
                 event, dx, dy, (unsigned long long)duration);
    }
    return event;
}
