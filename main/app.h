#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool ready;
    bool open;
    char ssid[33];
    char ip[16];
    uint8_t clients;
} network_state_t;

esp_err_t display_start(void);
bool display_ready(void);

esp_err_t network_start(void);
void network_get_state(network_state_t *out);

esp_err_t web_start(void);
