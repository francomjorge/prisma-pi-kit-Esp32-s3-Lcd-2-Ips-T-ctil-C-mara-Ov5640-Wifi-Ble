#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"
#include "touch_gesture.h"

/* CST816D on the shared I2C bus; gesture state never depends on display page. */
#define TOUCH_ADDR 0x15
#define TOUCH_CHIP_ID_REG 0xA7
#define TOUCH_REG 0x01

static const char *TAG="CS010Z_TOUCH";
static touch_hw_state_t s_state;
static touchz_t s_gesture;
static uint16_t s_last_action_x,s_last_action_y;

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
    touchz_init(&s_gesture);
    s_last_action_x=s_last_action_y=0;
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
 *  - detect horizontal swipe during finger drag at 34px (previously 55px);
 *  - confirm tap only after stable release and small displacement;
 *  - consume at most one action per contact;
 *  - debounce to suppress repeat/ghost navigation.
 */
/*
 * Samples CST816D coordinates every display loop tick. Release is confirmed
 * twice; I2C errors never become false releases. Short horizontal swipes are
 * accepted only after duration and direction checks; each contact emits at
 * most one event. Tap is release-only, requiring three down samples.
 */
int touch_hw_poll_action(void)
{
    if(!s_state.ready)return 0;
    uint8_t b[6]={0};
    const esp_err_t err=board_i2c_read_reg(TOUCH_ADDR,TOUCH_REG,b,sizeof(b),30);
    const uint64_t now=(uint64_t)esp_timer_get_time()/1000ULL;
    if(err!=ESP_OK){
        (void)touchz_feed(&s_gesture,false,false,0,0,now);
        return 0;
    }
    const uint8_t count=(uint8_t)(b[1]&0x0FU);
    if(count>1U)return 0; /* Invalid/multitouch data: no synthetic click. */
    const uint16_t x=(uint16_t)(((b[2]&0x0FU)<<8)|b[3]);
    const uint16_t y=(uint16_t)(((b[4]&0x0FU)<<8)|b[5]);
    const int action=touchz_feed(&s_gesture,true,count==1U,x,y,now);
    if(action){
        s_last_action_x=s_gesture.action_x;
        s_last_action_y=s_gesture.action_y;
        ESP_LOGD(TAG,"gesture=%d x=%u y=%u",action,
                 (unsigned)s_last_action_x,(unsigned)s_last_action_y);
    }
    return action;
}
