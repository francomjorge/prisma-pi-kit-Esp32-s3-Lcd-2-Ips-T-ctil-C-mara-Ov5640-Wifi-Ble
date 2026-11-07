#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Two physical LCD pages only: CONFIG/ADMIN at left (0), HOME at right (1).
 * Network, System, Files, Security, etc. remain inside the web Admin.
 * Swipe RIGHT from Home opens CONFIG. Swipe LEFT from CONFIG returns Home.
 * Touch on HOME freezes palette and accumulates original pixel glitch.
 */
enum {
    UI_PAGE_ADMIN = 0,
    UI_PAGE_HOME = 1,
    UI_PAGE_COUNT = 2,
    UI_EVENT_TAP = 1,
    UI_EVENT_SWIPE_LEFT = 2,
    UI_EVENT_SWIPE_RIGHT = 3
};

typedef enum {
    UI_CHANGE_NONE = 0,
    UI_CHANGE_PAGE = 1,
    UI_CHANGE_ADD_GLITCH = 2
} ui_change_t;

typedef struct {
    uint8_t page;
    uint8_t phase;
    uint32_t glitch_count;
    uint32_t last_animation_ms;
    uint32_t home_cooldown_until_ms;
} ui_model_t;

static inline void ui_model_init(ui_model_t *ui, uint32_t now_ms)
{
    if (!ui) return;
    ui->page = UI_PAGE_HOME;
    ui->phase = 0;
    ui->glitch_count = 0;
    ui->last_animation_ms = now_ms;
    ui->home_cooldown_until_ms = 0;
}

typedef struct {
    int x, y, size;
    uint8_t palette_index;
} ui_glitch_rect_t;

/* Pixel layout is intentionally the SAME formula as legacy main.c glitch().
 * Caller draws these on the already painted framebuffer (never re-clears). */
static inline ui_glitch_rect_t ui_model_glitch_rect(uint32_t glitch_index,
                                                    unsigned rect_index,
                                                    int touch_x, int touch_y)
{
    ui_glitch_rect_t out;
    out.x = touch_x + (int)((rect_index * 17U + glitch_index * 11U) % 55U) - 27;
    out.y = touch_y + (int)((rect_index * 23U + glitch_index * 7U) % 55U) - 27;
    out.size = 4 + (int)((rect_index + glitch_index) % 4U) * 3;
    out.palette_index = (uint8_t)((rect_index + glitch_index) % 5U);
    return out;
}

static inline bool ui_model_animation_step(ui_model_t *ui, uint32_t now_ms)
{
    if (!ui || ui->page != UI_PAGE_HOME || ui->glitch_count) return false;
    /* Original cadence: six ticks of 20 ms, i.e. 120 ms/colour shift. */
    if ((uint32_t)(now_ms - ui->last_animation_ms) < 120U) return false;
    ui->last_animation_ms = now_ms;
    ui->phase = (uint8_t)((ui->phase + 1U) % 5U);
    return true;
}

static inline ui_change_t ui_model_input(ui_model_t *ui, int action, uint32_t now_ms)
{
    if (!ui) return UI_CHANGE_NONE;
    /* Physical ordering is CONFIG(0) | HOME(1). No other LCD pages. */
    if (action == UI_EVENT_SWIPE_RIGHT && ui->page == UI_PAGE_HOME) {
        ui->page = UI_PAGE_ADMIN;
        ui->glitch_count = 0;
        ui->last_animation_ms = now_ms;
        return UI_CHANGE_PAGE;
    }
    if (action == UI_EVENT_SWIPE_LEFT && ui->page == UI_PAGE_ADMIN) {
        ui->page = UI_PAGE_HOME;
        ui->glitch_count = 0;
        ui->last_animation_ms = now_ms;
        ui->home_cooldown_until_ms = now_ms + 500U;
        return UI_CHANGE_PAGE;
    }
    if (action == UI_EVENT_TAP && ui->page == UI_PAGE_HOME &&
        now_ms >= ui->home_cooldown_until_ms) {
        ui->glitch_count++;
        return UI_CHANGE_ADD_GLITCH;
    }
    /* Secondary page taps are intentionally non-destructive. */
    return UI_CHANGE_NONE;
}
