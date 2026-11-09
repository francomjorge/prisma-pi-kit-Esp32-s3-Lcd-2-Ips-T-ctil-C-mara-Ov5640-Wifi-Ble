#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

#define PRISMA_SETTINGS_SCHEMA 1
#define PRISMA_SCAN_MAX 16
#define PRISMA_STORAGE_MAX_OPEN_FILES 12
#define PRISMA_EVENT_LOG_CAPACITY 64

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
    uint32_t sta_connect_attempts;
    uint32_t sta_disconnect_count;
    uint32_t sta_retry_in_ms;
    uint16_t sta_last_disconnect_reason;
    bool sta_retry_scheduled;
    bool sta_connecting;
    bool ap_running;
    uint32_t ap_connect_count;
    uint32_t ap_disconnect_count;
    uint16_t ap_last_disconnect_reason;
    uint64_t sta_last_connected_uptime_ms;
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
    bool probed;
    bool ready;
    uint8_t address;
    uint8_t who_am_i;
    uint8_t revision;
    char controller[24];
    char error[64];
} imu_hw_state_t;

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
    char internal_boot_status[24];
    storage_volume_state_t internal;
    storage_volume_state_t sd;
} storage_state_t;

typedef struct {
    uint32_t sequence;
    uint64_t uptime_ms;
    char level[8];
    char subsystem[20];
    char message[112];
} platform_event_t;

esp_err_t settings_start(void);
void settings_get(prisma_settings_t *out);
esp_err_t settings_set_identity(const char *friendly_name, const char *hostname);
esp_err_t settings_set_ap(const char *ssid, bool open, const char *password, uint8_t channel, uint8_t max_clients);
esp_err_t settings_set_sta(const char *ssid, const char *password, bool enabled);
esp_err_t settings_clear_sta(void);
esp_err_t settings_apply(const prisma_settings_t *candidate);
esp_err_t settings_factory_reset(void);

esp_err_t board_spi_start(void);
bool board_spi_ready(void);

esp_err_t board_i2c_start(void);
bool board_i2c_ready(void);
esp_err_t board_i2c_probe(uint8_t address, uint32_t timeout_ms);
esp_err_t board_i2c_read_reg(uint8_t address, uint8_t reg, uint8_t *data, size_t len, uint32_t timeout_ms);
esp_err_t board_i2c_write_reg(uint8_t address, uint8_t reg, const uint8_t *data, size_t len, uint32_t timeout_ms);

#define FILES_LCD_ROWS 4
#define FILES_LCD_PATH_MAX 90
typedef struct {
    char name[64];
    bool directory;
    uint32_t bytes;
} files_lcd_entry_t;
typedef struct {
    files_lcd_entry_t rows[FILES_LCD_ROWS];
    uint8_t count;
    uint32_t total;
} files_lcd_page_t;
esp_err_t files_lcd_list(const char *volume,const char *logical,
                         uint32_t offset,files_lcd_page_t *out);

esp_err_t storage_start(void);
esp_err_t storage_postboot_start(void);
void storage_get_state(storage_state_t *out);
esp_err_t storage_internal_initialize(void);
esp_err_t storage_sd_retry(void);
esp_err_t storage_sd_initialize(void);
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
esp_err_t network_retry_sta(void);

esp_err_t discovery_start(void);
void discovery_get_state(discovery_state_t *out);

typedef struct {
    bool supported,enabled;
    uint32_t photos,errors,last_bytes,last_ms;
    uint8_t size_index,quality_index,jpeg_quality;
    uint16_t width,height;
} photo_state_t;
void photo_state_get(photo_state_t *out);
esp_err_t photo_set_options(uint8_t size_index,uint8_t quality_index);
esp_err_t photo_power(bool on);
esp_err_t photo_take(httpd_req_t *request,char *saved,size_t cap);
esp_err_t photo_web_register(httpd_handle_t server);
esp_err_t camera_hw_probe(void);
void camera_hw_get_state(camera_hw_state_t *out);

esp_err_t touch_hw_probe(void);
void touch_hw_get_state(touch_hw_state_t *out);
/* 0 none, 1 tap, 2 swipe left, 3 swipe right; debounced events only. */
int touch_hw_poll_action(void);
/* Last accepted touch coordinate on the 240x320 LCD (for glitch origin). */
void touch_hw_last_point(uint16_t *x, uint16_t *y);

esp_err_t imu_hw_probe(void);
void imu_hw_get_state(imu_hw_state_t *out);

esp_err_t platform_log_start(void);
void platform_log_event(const char *level, const char *subsystem, const char *message);
size_t platform_log_copy(platform_event_t *out, size_t max_events);
uint32_t platform_log_total_events(void);

esp_err_t auth_start(void);
bool auth_is_enabled(void);
const char *auth_state(void);
esp_err_t auth_set_password(const char *current, const char *next_password);
esp_err_t auth_factory_reset(void);
esp_err_t auth_web_register(httpd_handle_t server);
esp_err_t auth_register_uri_handler(httpd_handle_t server, const httpd_uri_t *uri);

esp_err_t platform_web_register(void *server_handle);
esp_err_t diagnostics_web_register(httpd_handle_t server);
void ota_boot_check(bool core_ok);
esp_err_t ota_web_register(httpd_handle_t server);
esp_err_t web_start(void);
