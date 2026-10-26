#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "app.h"
#include "identity.h"

static const char *TAG = "CS006_SETTINGS";
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

static bool valid_settings(const prisma_settings_t *cfg)
{
    if (!cfg || cfg->schema != PRISMA_SETTINGS_SCHEMA) return false;
    if (!valid_printable(cfg->friendly_name, 32, false)) return false;
    if (!valid_hostname(cfg->hostname)) return false;
    if (!valid_printable(cfg->ap_ssid, 32, false)) return false;
    if (cfg->ap_channel < 1 || cfg->ap_channel > 13) return false;
    if (cfg->ap_max_clients < 1 || cfg->ap_max_clients > 4) return false;
    if (!cfg->ap_open) {
        size_t n = strlen(cfg->ap_password);
        if (n < 8 || n > 63 || !valid_printable(cfg->ap_password, 63, false)) return false;
    }
    if (cfg->sta_enabled) {
        if (!valid_printable(cfg->sta_ssid, 32, false)) return false;
        if (!valid_printable(cfg->sta_password, 63, true)) return false;
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
        if (e == ESP_OK && size == sizeof(loaded) && valid_settings(&loaded)) {
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

esp_err_t settings_apply(const prisma_settings_t *candidate)
{
    if (!valid_settings(candidate)) return ESP_ERR_INVALID_ARG;
    prisma_settings_t old = s_cfg;
    s_cfg = *candidate;
    esp_err_t e = save();
    if (e != ESP_OK) s_cfg = old;
    return e;
}

esp_err_t settings_factory_reset(void)
{
    prisma_settings_t old = s_cfg;
    defaults();
    esp_err_t e = save();
    if (e != ESP_OK) s_cfg = old;
    return e;
}

esp_err_t settings_set_identity(const char *friendly_name, const char *hostname)
{
    if (!valid_printable(friendly_name, 32, false) || !valid_hostname(hostname)) return ESP_ERR_INVALID_ARG;
    prisma_settings_t next = s_cfg;
    strlcpy(next.friendly_name, friendly_name, sizeof(next.friendly_name));
    strlcpy(next.hostname, hostname, sizeof(next.hostname));
    return settings_apply(&next);
}

esp_err_t settings_set_ap(const char *ssid, bool open, const char *password, uint8_t channel, uint8_t max_clients)
{
    prisma_settings_t next = s_cfg;
    strlcpy(next.ap_ssid, ssid ? ssid : "", sizeof(next.ap_ssid));
    next.ap_open = open;
    next.ap_channel = channel;
    next.ap_max_clients = max_clients;
    if (open) next.ap_password[0] = 0;
    else strlcpy(next.ap_password, password ? password : "", sizeof(next.ap_password));
    return settings_apply(&next);
}

esp_err_t settings_set_sta(const char *ssid, const char *password, bool enabled)
{
    prisma_settings_t next = s_cfg;
    next.sta_enabled = enabled;
    if (enabled) {
        strlcpy(next.sta_ssid, ssid ? ssid : "", sizeof(next.sta_ssid));
        strlcpy(next.sta_password, password ? password : "", sizeof(next.sta_password));
    } else {
        next.sta_ssid[0] = 0;
        next.sta_password[0] = 0;
    }
    return settings_apply(&next);
}

esp_err_t settings_clear_sta(void)
{
    return settings_set_sta("", "", false);
}
