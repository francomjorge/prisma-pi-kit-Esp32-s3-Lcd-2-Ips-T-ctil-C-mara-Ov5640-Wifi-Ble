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

typedef struct {
    bool ready;
    char hostname[64];
    char url[96];
} discovery_state_t;

typedef struct {
    bool probed;
    bool ready;
    bool expected;
    uint16_t pid;
    uint8_t midh;
    uint8_t midl;
    uint8_t ver;
    uint32_t xclk_hz;
    uint8_t fb_count;
    char fb_location[12];
    char sensor_name[24];
    char error[64];
} camera_hw_state_t;

typedef struct {
    bool probed;
    bool ready;
    uint8_t address;
    uint8_t chip_id;
    char controller[24];
    char error[64];
} touch_hw_state_t;

esp_err_t display_start(void);
bool display_ready(void);

esp_err_t network_start(void);
void network_get_state(network_state_t *out);

esp_err_t discovery_start(void);
void discovery_get_state(discovery_state_t *out);

esp_err_t camera_hw_probe(void);
void camera_hw_get_state(camera_hw_state_t *out);

esp_err_t touch_hw_probe(void);
void touch_hw_get_state(touch_hw_state_t *out);

esp_err_t web_start(void);
