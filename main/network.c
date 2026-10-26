#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "app.h"

static const char *TAG = "CS004_NET";
static esp_netif_t *s_ap;
static esp_netif_t *s_sta;
static network_state_t s_state;

static void refresh_state(void)
{
    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    s_state.open = cfg.ap_open;
    s_state.channel = cfg.ap_channel;
    s_state.max_clients = cfg.ap_max_clients;
    s_state.sta_configured = cfg.sta_enabled && cfg.sta_ssid[0];
    strlcpy(s_state.ssid, cfg.ap_ssid, sizeof(s_state.ssid));
    strlcpy(s_state.sta_ssid, cfg.sta_ssid, sizeof(s_state.sta_ssid));
    strlcpy(s_state.mode, "AP+STA", sizeof(s_state.mode));

    if (s_ap) {
        wifi_sta_list_t stations = {0};
        if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK) s_state.clients = (uint8_t)stations.num;
        esp_netif_ip_info_t info = {0};
        if (esp_netif_get_ip_info(s_ap, &info) == ESP_OK) snprintf(s_state.ip, sizeof(s_state.ip), IPSTR, IP2STR(&info.ip));
    }
    if (s_sta && s_state.sta_connected) {
        esp_netif_ip_info_t info = {0};
        if (esp_netif_get_ip_info(s_sta, &info) == ESP_OK) snprintf(s_state.sta_ip, sizeof(s_state.sta_ip), IPSTR, IP2STR(&info.ip));
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_state.sta_rssi = ap.rssi;
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            prisma_settings_t cfg = {0};
            settings_get(&cfg);
            if (cfg.sta_enabled && cfg.sta_ssid[0]) esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_state.sta_connected = false;
            s_state.sta_ip[0] = 0;
            s_state.sta_rssi = 0;
            prisma_settings_t cfg = {0};
            settings_get(&cfg);
            if (cfg.sta_enabled && cfg.sta_ssid[0]) esp_wifi_connect();
        } else if (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED) {
            refresh_state();
        }
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_state.sta_connected = true;
        refresh_state();
        ESP_LOGI(TAG, "STA connected SSID=%s IP=%s RSSI=%d", s_state.sta_ssid, s_state.sta_ip, s_state.sta_rssi);
    }
}

static esp_err_t apply_ap_config(const prisma_settings_t *cfg)
{
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, cfg->ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(cfg->ap_ssid);
    ap.ap.channel = cfg->ap_channel;
    ap.ap.max_connection = cfg->ap_max_clients;
    ap.ap.authmode = cfg->ap_open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;
    if (!cfg->ap_open) strlcpy((char *)ap.ap.password, cfg->ap_password, sizeof(ap.ap.password));
    return esp_wifi_set_config(WIFI_IF_AP, &ap);
}

static esp_err_t apply_sta_config(const prisma_settings_t *cfg)
{
    wifi_config_t sta = {0};
    if (cfg->sta_enabled && cfg->sta_ssid[0]) {
        strlcpy((char *)sta.sta.ssid, cfg->sta_ssid, sizeof(sta.sta.ssid));
        strlcpy((char *)sta.sta.password, cfg->sta_password, sizeof(sta.sta.password));
        sta.sta.pmf_cfg.capable = true;
        sta.sta.pmf_cfg.required = false;
    }
    return esp_wifi_set_config(WIFI_IF_STA, &sta);
}

void network_get_state(network_state_t *out)
{
    if (!out) return;
    refresh_state();
    *out = s_state;
}

esp_err_t network_start(void)
{
    memset(&s_state, 0, sizeof(s_state));
    prisma_settings_t cfg = {0};
    settings_get(&cfg);

    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    s_ap = esp_netif_create_default_wifi_ap();
    s_sta = esp_netif_create_default_wifi_sta();
    if (!s_ap || !s_sta) return ESP_FAIL;
    esp_netif_set_hostname(s_ap, cfg.hostname);
    esp_netif_set_hostname(s_sta, cfg.hostname);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&init)) != ESP_OK) return e;
    if ((e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return e;
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL);

    if ((e = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) return e;
    if ((e = apply_ap_config(&cfg)) != ESP_OK) return e;
    if ((e = apply_sta_config(&cfg)) != ESP_OK) return e;
    if ((e = esp_wifi_start()) != ESP_OK) return e;

    s_state.ready = true;
    refresh_state();
    ESP_LOGI(TAG, "AP+STA ready AP=%s security=%s channel=%u IP=%s STA=%s",
             cfg.ap_ssid, cfg.ap_open ? "open" : "WPA2", cfg.ap_channel,
             s_state.ip[0] ? s_state.ip : "-", cfg.sta_enabled ? cfg.sta_ssid : "disabled");
    return ESP_OK;
}

esp_err_t network_scan(wifi_ap_info_t *out, size_t max_results, size_t *count)
{
    if (!out || !count || max_results == 0) return ESP_ERR_INVALID_ARG;
    *count = 0;
    wifi_scan_config_t scan = {0};
    scan.show_hidden = false;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t e = esp_wifi_scan_start(&scan, true);
    if (e != ESP_OK) return e;
    uint16_t found = 0;
    if ((e = esp_wifi_scan_get_ap_num(&found)) != ESP_OK) return e;
    uint16_t n = found < max_results ? found : (uint16_t)max_results;
    wifi_ap_record_t records[PRISMA_SCAN_MAX] = {0};
    if (n > PRISMA_SCAN_MAX) n = PRISMA_SCAN_MAX;
    if (n && (e = esp_wifi_scan_get_ap_records(&n, records)) != ESP_OK) return e;
    for (uint16_t i = 0; i < n; ++i) {
        strlcpy(out[i].ssid, (const char *)records[i].ssid, sizeof(out[i].ssid));
        out[i].rssi = records[i].rssi;
        out[i].channel = records[i].primary;
        out[i].authmode = (uint8_t)records[i].authmode;
    }
    *count = n;
    return ESP_OK;
}

esp_err_t network_connect_sta(const char *ssid, const char *password)
{
    esp_err_t e = settings_set_sta(ssid, password ? password : "", true);
    if (e != ESP_OK) return e;
    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    if ((e = apply_sta_config(&cfg)) != ESP_OK) return e;
    s_state.sta_connected = false;
    s_state.sta_ip[0] = 0;
    esp_wifi_disconnect();
    e = esp_wifi_connect();
    refresh_state();
    return e;
}

esp_err_t network_forget_sta(void)
{
    esp_err_t e = settings_clear_sta();
    if (e != ESP_OK) return e;
    esp_wifi_disconnect();
    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    apply_sta_config(&cfg);
    s_state.sta_connected = false;
    s_state.sta_ip[0] = 0;
    s_state.sta_ssid[0] = 0;
    s_state.sta_rssi = 0;
    refresh_state();
    return ESP_OK;
}
