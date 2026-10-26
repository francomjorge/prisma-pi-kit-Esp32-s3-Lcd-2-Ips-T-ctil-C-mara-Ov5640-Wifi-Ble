#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "app.h"
#include "identity.h"

static const char *TAG = "CS004_SETTINGS";
static const char *NS = "prisma_cfg";
static const char *KEY = "settings";
static prisma_settings_t s_cfg;

static bool valid_hostname(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n < 1 || n > 32 || s[0] == '-' || s[n - 1] == '-') return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '-')) return false;
    }
    return true;
}

static bool valid_printable(const char *s, size_t max_len, bool allow_empty)
{
    size_t n = s ? strlen(s) : 0;
    if ((!allow_empty && n == 0) || n > max_len) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126 || c == '"' || c == '\\') return false;
    }
    return true;
}

static void defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.schema = PRISMA_SETTINGS_SCHEMA;
    strlcpy(s_cfg.friendly_name, PRISMA_PRODUCT, sizeof(s_cfg.friendly_name));
    strlcpy(s_cfg.hostname, PRISMA_HOSTNAME, sizeof(s_cfg.hostname));
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK) {
        snprintf(s_cfg.ap_ssid, sizeof(s_cfg.ap_ssid), PRISMA_AP_PREFIX "%02X%02X%02X", mac[3], mac[4], mac[5]);
    } else {
        strlcpy(s_cfg.ap_ssid, "PRISMA-PI-CAMERA-SYNTH", sizeof(s_cfg.ap_ssid));
    }
    s_cfg.ap_open = true;
    s_cfg.ap_channel = 6;
    s_cfg.ap_max_clients = 4;
    s_cfg.sta_enabled = false;
}

static esp_err_t save(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, KEY, &s_cfg, sizeof(s_cfg));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

esp_err_t settings_start(void)
{
    defaults();
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_OK) {
        prisma_settings_t loaded = {0};
        size_t size = sizeof(loaded);
        e = nvs_get_blob(h, KEY, &loaded, &size);
        nvs_close(h);
        if (e == ESP_OK && size == sizeof(loaded) && loaded.schema == PRISMA_SETTINGS_SCHEMA &&
            valid_hostname(loaded.hostname) && valid_printable(loaded.ap_ssid, 32, false) &&
            loaded.ap_channel >= 1 && loaded.ap_channel <= 13 && loaded.ap_max_clients >= 1 && loaded.ap_max_clients <= 4) {
            s_cfg = loaded;
            ESP_LOGI(TAG, "Loaded settings schema=%u hostname=%s AP=%s", (unsigned)s_cfg.schema, s_cfg.hostname, s_cfg.ap_ssid);
            return ESP_OK;
        }
    }
    defaults();
    e = save();
    ESP_LOGI(TAG, "Created default settings hostname=%s AP=%s", s_cfg.hostname, s_cfg.ap_ssid);
    return e;
}

void settings_get(prisma_settings_t *out)
{
    if (out) *out = s_cfg;
}

esp_err_t settings_set_identity(const char *friendly_name, const char *hostname)
{
    if (!valid_printable(friendly_name, 32, false) || !valid_hostname(hostname)) return ESP_ERR_INVALID_ARG;
    prisma_settings_t old = s_cfg;
    strlcpy(s_cfg.friendly_name, friendly_name, sizeof(s_cfg.friendly_name));
    strlcpy(s_cfg.hostname, hostname, sizeof(s_cfg.hostname));
    esp_err_t e = save();
    if (e != ESP_OK) s_cfg = old;
    return e;
}

esp_err_t settings_set_ap(const char *ssid, bool open, const char *password, uint8_t channel, uint8_t max_clients)
{
    if (!valid_printable(ssid, 32, false) || channel < 1 || channel > 13 || max_clients < 1 || max_clients > 4) return ESP_ERR_INVALID_ARG;
    if (!open) {
        size_t n = password ? strlen(password) : 0;
        if (n < 8 || n > 63 || !valid_printable(password, 63, false)) return ESP_ERR_INVALID_ARG;
    }
    prisma_settings_t old = s_cfg;
    strlcpy(s_cfg.ap_ssid, ssid, sizeof(s_cfg.ap_ssid));
    s_cfg.ap_open = open;
    s_cfg.ap_channel = channel;
    s_cfg.ap_max_clients = max_clients;
    if (open) s_cfg.ap_password[0] = 0;
    else strlcpy(s_cfg.ap_password, password, sizeof(s_cfg.ap_password));
    esp_err_t e = save();
    if (e != ESP_OK) s_cfg = old;
    return e;
}

esp_err_t settings_set_sta(const char *ssid, const char *password, bool enabled)
{
    if (enabled && (!valid_printable(ssid, 32, false) || !valid_printable(password ? password : "", 63, true))) return ESP_ERR_INVALID_ARG;
    prisma_settings_t old = s_cfg;
    s_cfg.sta_enabled = enabled;
    if (enabled) {
        strlcpy(s_cfg.sta_ssid, ssid, sizeof(s_cfg.sta_ssid));
        strlcpy(s_cfg.sta_password, password ? password : "", sizeof(s_cfg.sta_password));
    } else {
        s_cfg.sta_ssid[0] = 0;
        s_cfg.sta_password[0] = 0;
    }
    esp_err_t e = save();
    if (e != ESP_OK) s_cfg = old;
    return e;
}

esp_err_t settings_clear_sta(void)
{
    return settings_set_sta("", "", false);
}
