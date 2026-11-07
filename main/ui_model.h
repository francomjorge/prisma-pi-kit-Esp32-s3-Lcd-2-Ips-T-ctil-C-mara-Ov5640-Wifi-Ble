#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Exact visible behavior copied from the validated PRISMA-PI LITE UI:
 * - -PI cycles until first tap
 * - first tap freezes the palette
 * - subsequent taps add pixel-glitch blocks without clearing the framebuffer
 * - swiping away and returning resets glitch and restarts cycling
 * - swipe LEFT from HOME opens ADMIN
 * - a home cooldown prevents a swipe release being interpreted as a tap.
 * Hardware polling/coordinates remain separate: this is pure portable C.
 */
enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_ADMIN = 1,
    UI_PAGE_NETWORK = 2,
    UI_PAGE_SYSTEM = 3,
    UI_PAGE_COUNT = 4,
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
    if (action == UI_EVENT_SWIPE_LEFT || action == UI_EVENT_SWIPE_RIGHT) {
        if (action == UI_EVENT_SWIPE_LEFT) {
            if (ui->page < UI_PAGE_COUNT - 1) ui->page++;
            else return UI_CHANGE_NONE;
        } else {
            if (ui->page > UI_PAGE_HOME) ui->page--;
            else return UI_CHANGE_NONE;
        }
        ui->glitch_count = 0;
        ui->last_animation_ms = now_ms;
        /* Matches prior HOME_COOLDOWN_TICKS = 25, 20ms loop. */
        if (ui->page == UI_PAGE_HOME)
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
