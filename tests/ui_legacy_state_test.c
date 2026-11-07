#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ui_model.h"

static void test_original_home_sequence(void)
{
    ui_model_t ui;
    ui_model_init(&ui, 1000);
    assert(ui.page == UI_PAGE_HOME && ui.phase == 0 && ui.glitch_count == 0);
    assert(!ui_model_animation_step(&ui, 1119));
    assert(ui_model_animation_step(&ui, 1120));
    assert(ui.phase == 1);

    assert(ui_model_input(&ui, UI_EVENT_TAP, 1140) == UI_CHANGE_ADD_GLITCH);
    assert(ui.glitch_count == 1);
    const unsigned held_phase = ui.phase;
    assert(!ui_model_animation_step(&ui, 2000));
    assert(ui.phase == held_phase);

    assert(ui_model_input(&ui, UI_EVENT_TAP, 2200) == UI_CHANGE_ADD_GLITCH);
    assert(ui_model_input(&ui, UI_EVENT_TAP, 2500) == UI_CHANGE_ADD_GLITCH);
    assert(ui.glitch_count == 3);
    assert(!ui_model_animation_step(&ui, 5000));

    /* RIGHT opens the page on the left. */
    assert(ui_model_input(&ui, UI_EVENT_SWIPE_RIGHT, 5100) == UI_CHANGE_PAGE);
    assert(ui.page == UI_PAGE_ADMIN && ui.glitch_count == 0);
    assert(ui_model_input(&ui, UI_EVENT_TAP, 5200) == UI_CHANGE_NONE);
    assert(ui.page == UI_PAGE_ADMIN);
    assert(ui_model_input(&ui, UI_EVENT_SWIPE_LEFT, 5600) == UI_CHANGE_PAGE);
    assert(ui.page == UI_PAGE_HOME && ui.glitch_count == 0);

    assert(ui_model_input(&ui, UI_EVENT_TAP, 5800) == UI_CHANGE_NONE);
    assert(ui_model_animation_step(&ui, 5800));
    assert(ui_model_input(&ui, UI_EVENT_TAP, 6100) == UI_CHANGE_ADD_GLITCH);
    assert(ui.glitch_count == 1);
}

static void test_navigation_is_bounded(void)
{
    ui_model_t ui;
    ui_model_init(&ui, 0);
    assert(UI_PAGE_COUNT == 2);
    assert(UI_PAGE_ADMIN == 0 && UI_PAGE_HOME == 1);
    assert(ui_model_input(&ui, UI_EVENT_SWIPE_LEFT, 100) == UI_CHANGE_NONE);
    assert(ui.page == UI_PAGE_HOME);
    assert(ui_model_input(&ui, UI_EVENT_SWIPE_RIGHT, 200) == UI_CHANGE_PAGE);
    assert(ui.page == UI_PAGE_ADMIN);
    assert(ui_model_input(&ui, UI_EVENT_SWIPE_RIGHT, 400) == UI_CHANGE_NONE);
    assert(ui.page == UI_PAGE_ADMIN);
    assert(ui_model_input(&ui, UI_EVENT_SWIPE_LEFT, 700) == UI_CHANGE_PAGE);
    assert(ui.page == UI_PAGE_HOME);
    assert(ui_model_input(&ui, UI_EVENT_SWIPE_LEFT, 900) == UI_CHANGE_NONE);
    assert(ui.page == UI_PAGE_HOME);
}

static void test_identical_legacy_glitch_pattern(void)
{
    /* Exact formula from prisma-pi-lite/main/main.c glitch(), unchanged. */
    const ui_glitch_rect_t first = ui_model_glitch_rect(0, 0, 120, 175);
    assert(first.x == 93 && first.y == 148);
    assert(first.size == 4 && first.palette_index == 0);

    const ui_glitch_rect_t second_tap = ui_model_glitch_rect(1, 0, 120, 175);
    assert(second_tap.x == 104 && second_tap.y == 155);
    assert(second_tap.size == 7 && second_tap.palette_index == 1);

    uint8_t pixels[240][240] = {{0}};
    size_t count_first = 0, count_second = 0;
    for (uint32_t burst = 0; burst < 2; ++burst) {
        for (unsigned i = 0; i < 9; ++i) {
            ui_glitch_rect_t b = ui_model_glitch_rect(burst,i,120,175);
            for (int y = b.y; y < b.y + b.size; ++y)
                for (int x = b.x; x < b.x + b.size; ++x)
                    if (x >= 0 && x < 240 && y >= 40 && y < 280)
                        pixels[y - 40][x] = 1;
        }
        size_t n = 0;
        for (int y=0; y<240; ++y) for (int x=0; x<240; ++x) n += pixels[y][x];
        if (burst == 0) count_first = n;
        else count_second = n;
    }
    assert(count_first > 0 && count_second > count_first);
    /* No clearing between bursts: total occupied pixels strictly grows. */
}

int main(void)
{
    test_original_home_sequence();
    test_navigation_is_bounded();
    test_identical_legacy_glitch_pattern();
    puts("PRISMA-PI LEGACY UI: phase pause, accumulating glitch, two LCD pages CONFIG | HOME, reset PASS");
    return 0;
}
