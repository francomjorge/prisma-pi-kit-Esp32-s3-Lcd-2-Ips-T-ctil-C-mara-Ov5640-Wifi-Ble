#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "app.h"
#include "identity.h"

static const char *TAG = "CS001_NET";
static esp_netif_t *s_ap;
static network_state_t s_state;

static esp_err_t refresh_state(void)
{
    if (!s_ap) return ESP_ERR_INVALID_STATE;

    wifi_sta_list_t stations = {0};
    if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK) s_state.clients = (uint8_t)stations.num;

    esp_netif_ip_info_t info = {0};
    if (esp_netif_get_ip_info(s_ap, &info) == ESP_OK) {
        snprintf(s_state.ip, sizeof(s_state.ip), IPSTR, IP2STR(&info.ip));
    }
    return ESP_OK;
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
    s_state.open = true;

    esp_err_t e = esp_netif_init();
    if (e != ESP_OK) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    s_ap = esp_netif_create_default_wifi_ap();
    if (!s_ap) return ESP_FAIL;
    esp_netif_set_hostname(s_ap, PRISMA_HOSTNAME);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&init)) != ESP_OK) return e;
    if ((e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return e;

    uint8_t mac[6] = {0};
    if ((e = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP)) != ESP_OK) return e;
    snprintf(s_state.ssid, sizeof(s_state.ssid), PRISMA_AP_PREFIX "%02X%02X%02X", mac[3], mac[4], mac[5]);

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.ap.ssid, s_state.ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(s_state.ssid);
    cfg.ap.channel = 6;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_OPEN;
    cfg.ap.pmf_cfg.required = false;

    if ((e = esp_wifi_set_mode(WIFI_MODE_AP)) != ESP_OK) return e;
    if ((e = esp_wifi_set_config(WIFI_IF_AP, &cfg)) != ESP_OK) return e;
    if ((e = esp_wifi_start()) != ESP_OK) return e;

    s_state.ready = true;
    refresh_state();
    ESP_LOGI(TAG, "Open AP ready SSID=%s IP=%s max_clients=%u hostname=%s",
             s_state.ssid, s_state.ip, cfg.ap.max_connection, PRISMA_HOSTNAME);
    return ESP_OK;
}
