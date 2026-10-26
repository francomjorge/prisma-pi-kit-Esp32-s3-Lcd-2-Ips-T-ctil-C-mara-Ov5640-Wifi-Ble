#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define APP_BUILD "005.0"
extern char app_ssid[32], app_password[16];
extern bool network_ready;
extern bool camera_probed,camera_ready,camera_expected;
extern uint16_t camera_pid;
extern uint8_t camera_midh,camera_midl,camera_ver;
extern char camera_sensor_name[24],camera_error[64];
extern bool camera_capture_attempted,camera_capture_ready;
extern uint16_t camera_frame_width,camera_frame_height;
extern uint32_t camera_frame_bytes,camera_capture_ms;
extern char camera_frame_format[12],camera_capture_error[64];
extern bool camera_preview_active;
extern uint32_t camera_preview_frames,camera_preview_capture_ms,camera_preview_decode_ms,camera_preview_frame_ms;
extern uint16_t camera_preview_fps;
extern char camera_preview_error[64];
extern uint8_t camera_jpeg_quality,camera_fb_count;
extern char camera_grab_mode[16];
extern uint32_t ui_preview_display_ms,ui_preview_loop_ms,touch_taps,touch_swipes;
extern uint16_t ui_preview_fps,touch_poll_ms,touch_swipe_threshold_px,touch_debounce_ms;
esp_err_t display_start(void);
bool display_ready(void);
bool touch_ready(void);
esp_err_t camera_start(void);
esp_err_t camera_capture_test(void);
esp_err_t camera_preview_frame(uint8_t *out,size_t out_bytes);
void camera_preview_set_active(bool active);
const char *camera_state_text(void);
const char *camera_capture_state_text(void);
const char *camera_preview_state_text(void);
