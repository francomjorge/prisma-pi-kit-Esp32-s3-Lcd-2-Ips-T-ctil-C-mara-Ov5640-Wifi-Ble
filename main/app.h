#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define PRISMA_SETTINGS_SCHEMA 1
#define PRISMA_SCAN_MAX 16
#define PRISMA_STORAGE_MAX_OPEN_FILES 12

typedef struct {
    uint32_t schema;
    char friendly_name[33];
    char hostname[33];
    char ap_ssid[33];
    char ap_password[64];
    bool ap_open;
    uint8_t ap_channel;
    uint8_t ap_max_clients;
    bool sta_enabled;
    char sta_ssid[33];
    char sta_password[64];
} prisma_settings_t;

typedef struct {
    bool ready;
    bool open;
    bool sta_configured;
    bool sta_connected;
    char mode[12];
    char ssid[33];
    char ip[16];
    uint8_t clients;
    uint8_t channel;
    uint8_t max_clients;
    char sta_ssid[33];
    char sta_ip[16];
    int8_t sta_rssi;
} network_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t authmode;
} wifi_ap_info_t;

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

typedef struct {
    bool available;
    bool mounted;
    bool writable;
    char id[12];
    char fs[16];
    char mount[16];
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    unsigned int max_open_files;
    char error[80];
} storage_volume_state_t;

typedef struct {
    bool shared_spi_ready;
    storage_volume_state_t internal;
    storage_volume_state_t sd;
} storage_state_t;

esp_err_t settings_start(void);
void settings_get(prisma_settings_t *out);
esp_err_t settings_set_identity(const char *friendly_name, const char *hostname);
esp_err_t settings_set_ap(const char *ssid, bool open, const char *password, uint8_t channel, uint8_t max_clients);
esp_err_t settings_set_sta(const char *ssid, const char *password, bool enabled);
esp_err_t settings_clear_sta(void);

esp_err_t board_spi_start(void);
bool board_spi_ready(void);

esp_err_t storage_start(void);
void storage_get_state(storage_state_t *out);
esp_err_t storage_internal_initialize(void);
esp_err_t storage_sd_retry(void);
const char *storage_volume_root(const char *volume);
bool storage_volume_mounted(const char *volume);
bool storage_lock(uint32_t timeout_ms);
void storage_unlock(void);

esp_err_t display_start(void);
bool display_ready(void);

esp_err_t network_start(void);
void network_get_state(network_state_t *out);
esp_err_t network_scan(wifi_ap_info_t *out, size_t max_results, size_t *count);
esp_err_t network_connect_sta(const char *ssid, const char *password);
esp_err_t network_forget_sta(void);

esp_err_t discovery_start(void);
void discovery_get_state(discovery_state_t *out);

esp_err_t camera_hw_probe(void);
void camera_hw_get_state(camera_hw_state_t *out);

esp_err_t touch_hw_probe(void);
void touch_hw_get_state(touch_hw_state_t *out);

esp_err_t web_start(void);
