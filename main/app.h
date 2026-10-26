#pragma once
#include <stdbool.h>
#include "esp_err.h"
extern char app_ssid[32], app_password[16];
extern bool network_ready;
esp_err_t display_start(void);
bool display_ready(void);
bool touch_ready(void);
