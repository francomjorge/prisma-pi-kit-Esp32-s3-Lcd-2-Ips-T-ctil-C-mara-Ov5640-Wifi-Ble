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
/* Shorter deliberate horizontal gesture; do not relax tap filtering. */
#define TOUCH_SWIPE_THRESHOLD_PX 30

static const char *TAG = "CS010R_TOUCH";
static touch_hw_state_t s_state;
static bool s_pressed;
static bool s_consumed;
static uint16_t s_x0, s_y0, s_x_last, s_y_last;
static uint8_t s_samples, s_release_samples;
static uint64_t s_press_ms, s_event_ms;
static uint16_t s_last_action_x, s_last_action_y;

void touch_hw_last_point(uint16_t *x, uint16_t *y)
{
    if (x) *x = s_last_action_x;
    if (y) *y = s_last_action_y;
}

void touch_hw_get_state(touch_hw_state_t *out)
{
    if (out) *out = s_state;
}

esp_err_t touch_hw_probe(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_pressed = false;
    s_consumed = false;
    s_samples = 0;
    s_release_samples = 0;
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
 * Build 58 touch behavior adapted to CST816D:
 *  - detect a short swipe during finger-down (>30px), with axis dominance;
 *  - confirm tap only after stable release and small displacement;
 *  - consume at most one action per contact;
 *  - debounce to suppress repeat/ghost navigation.
 */
int touch_hw_poll_action(void)
{
    if (!s_state.ready) return 0;
    uint8_t b[6] = {0};
    if (board_i2c_read_reg(TOUCH_ADDR, GESTURE_REG, b, sizeof(b), 30) != ESP_OK)
        return 0;
    const uint64_t now = (uint64_t)esp_timer_get_time() / 1000ULL;
    const bool finger = (b[1] & 0x0f) == 1U;
    const uint16_t x = (uint16_t)(((b[2] & 0x0f) << 8) | b[3]);
    const uint16_t y = (uint16_t)(((b[4] & 0x0f) << 8) | b[5]);

    if (finger) {
        s_release_samples = 0;
        if (!s_pressed) {
            s_pressed = true;
            s_consumed = false;
            s_press_ms = now;
            s_x0 = s_x_last = x;
            s_y0 = s_y_last = y;
            s_samples = 1;
            return 0;
        }
        s_x_last = x;
        s_y_last = y;
        if (s_samples < UINT8_MAX) s_samples++;
        const int dx = (int)x - (int)s_x0;
        const int dy = (int)y - (int)s_y0;
        if (!s_consumed && s_samples >= 3 && (now - s_press_ms) >= 45U &&
            (now - s_press_ms) < 1400U &&
            abs(dx) >= TOUCH_SWIPE_THRESHOLD_PX && abs(dx) > abs(dy) + 10 &&
            (!s_event_ms || now - s_event_ms >= TOUCH_LOCKOUT_MS)) {
            s_consumed = true;
            s_last_action_x = x;
            s_last_action_y = y;
            s_event_ms = now;
            const int event = dx < 0 ? 2 : 3;
            ESP_LOGI(TAG, "swipe=%d dx=%d dy=%d", event, dx, dy);
            return event;
        }
        return 0;
    }

    if (!s_pressed) return 0;
    if (++s_release_samples < 3) return 0;
    s_pressed = false;
    s_release_samples = 0;
    if (s_consumed) return 0;
    const uint64_t duration = now - s_press_ms;
    const int dx = (int)s_x_last - (int)s_x0;
    const int dy = (int)s_y_last - (int)s_y0;
    if (s_samples < 2 || duration > 700U ||
        abs(dx) > 18 || abs(dy) > 18 ||
        (s_event_ms && now - s_event_ms < TOUCH_LOCKOUT_MS))
        return 0;
    s_last_action_x = s_x0;
    s_last_action_y = s_y0;
    s_event_ms = now;
    ESP_LOGI(TAG, "tap x=%u y=%u", (unsigned)s_x0, (unsigned)s_y0);
    return 1;
}
