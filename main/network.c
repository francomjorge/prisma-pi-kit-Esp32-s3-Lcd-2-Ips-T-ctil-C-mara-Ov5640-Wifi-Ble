#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "app.h"

#define STA_RETRY_MIN_MS 2000U
#define STA_RETRY_MAX_MS 30000U

static const char *TAG = "CS010_NET";
static esp_netif_t *s_ap;
static esp_netif_t *s_sta;
static esp_timer_handle_t s_retry_timer;
static network_state_t s_state;
static unsigned int s_retry_step;
static int64_t s_retry_due_us;
static bool s_skip_next_disconnect;

static bool sta_wanted(void)
{
    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    return cfg.sta_enabled && cfg.sta_ssid[0] != '\0';
}

static void cancel_retry(void)
{
    if (s_retry_timer && esp_timer_is_active(s_retry_timer)) {
        esp_timer_stop(s_retry_timer);
    }
    s_retry_due_us = 0;
    s_state.sta_retry_scheduled = false;
    s_state.sta_retry_in_ms = 0;
}

static void schedule_retry(void)
{
    if (!sta_wanted() || !s_retry_timer) return;
    if (esp_timer_is_active(s_retry_timer)) return;

    uint32_t delay_ms = STA_RETRY_MIN_MS;
    for (unsigned int i = 0; i < s_retry_step; ++i) {
        delay_ms *= 2U;
        if (delay_ms >= STA_RETRY_MAX_MS) {
            delay_ms = STA_RETRY_MAX_MS;
            break;
        }
    }
    if (s_retry_step < 5U) ++s_retry_step;

    if (esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000ULL) == ESP_OK) {
        s_retry_due_us = esp_timer_get_time() + (int64_t)delay_ms * 1000LL;
        s_state.sta_retry_scheduled = true;
        s_state.sta_retry_in_ms = delay_ms;
        ESP_LOGW(TAG, "STA retry scheduled in %lu ms (reason=%u)",
                 (unsigned long)delay_ms, (unsigned)s_state.sta_last_disconnect_reason);
    } else {
        s_state.sta_retry_scheduled = false;
        s_state.sta_retry_in_ms = 0;
        s_retry_due_us = 0;
        platform_log_event("ERROR", "network", "STA retry timer could not be started");
    }
}

static esp_err_t connect_sta_now(void)
{
    if (!sta_wanted()) return ESP_ERR_INVALID_STATE;
    ++s_state.sta_connect_attempts;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "STA connect request error: %s", esp_err_to_name(err));
        schedule_retry();
    }
    return err;
}

static void retry_timer_callback(void *arg)
{
    (void)arg;
    s_retry_due_us = 0;
    s_state.sta_retry_scheduled = false;
    s_state.sta_retry_in_ms = 0;
    if (sta_wanted() && !s_state.sta_connected) {
        (void)connect_sta_now();
    }
}

static void refresh_state(void)
{
    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    s_state.sta_configured = cfg.sta_enabled && cfg.sta_ssid[0];
    strlcpy(s_state.sta_ssid, cfg.sta_ssid, sizeof(s_state.sta_ssid));
    strlcpy(s_state.mode, "AP+STA", sizeof(s_state.mode));

    /* Read the running AP configuration, not pending settings awaiting reboot. */
    wifi_config_t ap_config = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_config) == ESP_OK) {
        s_state.open = ap_config.ap.authmode == WIFI_AUTH_OPEN;
        s_state.max_clients = ap_config.ap.max_connection;
        strlcpy(s_state.ssid, (const char *)ap_config.ap.ssid, sizeof(s_state.ssid));
        s_state.channel = ap_config.ap.channel;
    } else {
        s_state.open = cfg.ap_open;
        s_state.max_clients = cfg.ap_max_clients;
        s_state.channel = cfg.ap_channel;
        strlcpy(s_state.ssid, cfg.ap_ssid, sizeof(s_state.ssid));
    }

    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &secondary) == ESP_OK) {
        s_state.channel = primary;
    }

    if (s_retry_due_us > 0 && s_state.sta_retry_scheduled) {
        int64_t left_us = s_retry_due_us - esp_timer_get_time();
        s_state.sta_retry_in_ms = left_us > 0 ? (uint32_t)((left_us + 999) / 1000) : 0;
    } else {
        s_state.sta_retry_in_ms = 0;
    }

    if (s_ap) {
        wifi_sta_list_t stations = {0};
        if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK) s_state.clients = (uint8_t)stations.num;
        esp_netif_ip_info_t info = {0};
        if (esp_netif_get_ip_info(s_ap, &info) == ESP_OK)
            snprintf(s_state.ip, sizeof(s_state.ip), IPSTR, IP2STR(&info.ip));
    }
    if (s_sta && s_state.sta_connected) {
        esp_netif_ip_info_t info = {0};
        if (esp_netif_get_ip_info(s_sta, &info) == ESP_OK)
            snprintf(s_state.sta_ip, sizeof(s_state.sta_ip), IPSTR, IP2STR(&info.ip));
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_state.sta_rssi = ap.rssi;
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        if (sta_wanted()) (void)connect_sta_now();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_state.sta_connected = false;
        s_state.sta_ip[0] = '\0';
        s_state.sta_rssi = 0;
        ++s_state.sta_disconnect_count;
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)data;
        s_state.sta_last_disconnect_reason = event ? event->reason : 0;
        if (s_skip_next_disconnect) {
            s_skip_next_disconnect = false;
            return;
        }
        if (sta_wanted()) {
            char msg[112];
            snprintf(msg, sizeof(msg),
                     "STA disconnected reason=%u; backoff scheduled",
                     (unsigned)s_state.sta_last_disconnect_reason);
            platform_log_event("WARN", "network", msg);
            schedule_retry();
        } else {
            cancel_retry();
        }
    } else if (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED) {
        refresh_state();
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_state.sta_connected = true;
        s_state.sta_last_connected_uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
        s_retry_step = 0;
        cancel_retry();
        refresh_state();
        ESP_LOGI(TAG, "STA connected, IP=%s, RSSI=%d", s_state.sta_ip, s_state.sta_rssi);
        platform_log_event("INFO", "network", "STA acquired IP; retry backoff reset");
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
    if (!cfg->ap_open)
        strlcpy((char *)ap.ap.password, cfg->ap_password, sizeof(ap.ap.password));
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
    s_retry_step = 0;
    s_skip_next_disconnect = false;
    s_retry_due_us = 0;

    prisma_settings_t cfg = {0};
    settings_get(&cfg);

    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    s_ap = esp_netif_create_default_wifi_ap();
    s_sta = esp_netif_create_default_wifi_sta();
    if (!s_ap || !s_sta) return ESP_FAIL;
    (void)esp_netif_set_hostname(s_ap, cfg.hostname);
    (void)esp_netif_set_hostname(s_sta, cfg.hostname);

    esp_timer_create_args_t timer_args = {
        .callback = retry_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "sta_retry"
    };
    e = esp_timer_create(&timer_args, &s_retry_timer);
    if (e != ESP_OK) return e;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&init)) != ESP_OK) return e;
    if ((e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return e;
    esp_wifi_set_ps(WIFI_PS_NONE);
    if ((e = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event, NULL)) != ESP_OK) return e;
    if ((e = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        ip_event, NULL)) != ESP_OK) return e;

    if ((e = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) return e;
    if ((e = apply_ap_config(&cfg)) != ESP_OK) return e;
    if ((e = apply_sta_config(&cfg)) != ESP_OK) return e;
    if ((e = esp_wifi_start()) != ESP_OK) return e;

    s_state.ready = true;
    refresh_state();
    ESP_LOGI(TAG, "AP+STA ready; AP=%s security=%s channel=%u IP=%s",
             s_state.ssid, s_state.open ? "open" : "WPA2",
             s_state.channel, s_state.ip[0] ? s_state.ip : "-");
    platform_log_event("INFO", "network", "SoftAP started; STA reconnect backoff enabled");
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

esp_err_t network_retry_sta(void)
{
    if (!s_state.ready || !sta_wanted()) return ESP_ERR_INVALID_STATE;
    if (s_state.sta_connected) return ESP_OK;
    cancel_retry();
    s_retry_step = 0;
    esp_err_t e = connect_sta_now();
    if (e == ESP_OK)
        platform_log_event("INFO", "network", "Manual STA connection attempt requested");
    return e;
}

esp_err_t network_connect_sta(const char *ssid, const char *password)
{
    esp_err_t e = settings_set_sta(ssid, password ? password : "", true);
    if (e != ESP_OK) return e;
    cancel_retry();
    s_retry_step = 0;

    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    if (s_state.sta_connected) {
        s_skip_next_disconnect = true;
        (void)esp_wifi_disconnect();
    } else {
        s_skip_next_disconnect = false;
    }
    if ((e = apply_sta_config(&cfg)) != ESP_OK) return e;
    s_state.sta_connected = false;
    s_state.sta_ip[0] = 0;
    e = connect_sta_now();
    refresh_state();
    return e;
}

esp_err_t network_forget_sta(void)
{
    esp_err_t e = settings_clear_sta();
    if (e != ESP_OK) return e;
    cancel_retry();
    s_retry_step = 0;
    s_skip_next_disconnect = false;
    (void)esp_wifi_disconnect();
    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    e = apply_sta_config(&cfg);
    s_state.sta_connected = false;
    s_state.sta_ip[0] = 0;
    s_state.sta_ssid[0] = 0;
    s_state.sta_rssi = 0;
    refresh_state();
    platform_log_event("INFO", "network", "STA forgotten; SoftAP remains active");
    return e;
}
