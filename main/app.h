#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define APP_BUILD "002.0"
extern char app_ssid[32], app_password[16];
extern bool network_ready;
extern bool camera_probed,camera_ready,camera_expected;
extern uint16_t camera_pid;
extern uint8_t camera_midh,camera_midl,camera_ver;
extern char camera_sensor_name[24],camera_error[64];
esp_err_t display_start(void);
bool display_ready(void);
bool touch_ready(void);
esp_err_t camera_start(void);
const char *camera_state_text(void);
