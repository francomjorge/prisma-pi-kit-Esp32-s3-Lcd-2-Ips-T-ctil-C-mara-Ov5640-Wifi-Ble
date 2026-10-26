#include <stdio.h>
#include <string.h>
#include "mdns.h"
#include "esp_log.h"
#include "app.h"
#include "identity.h"

static const char *TAG = "CS004_MDNS";
static discovery_state_t s_state;

void discovery_get_state(discovery_state_t *out)
{
    if (out) *out = s_state;
}

esp_err_t discovery_start(void)
{
    memset(&s_state, 0, sizeof(s_state));
    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    strlcpy(s_state.hostname, cfg.hostname, sizeof(s_state.hostname));
    snprintf(s_state.url, sizeof(s_state.url), "http://%s.local/", cfg.hostname);

    esp_err_t err = mdns_init();
    if (err != ESP_OK) return err;
    if ((err = mdns_hostname_set(cfg.hostname)) != ESP_OK) return err;
    if ((err = mdns_instance_name_set(cfg.friendly_name)) != ESP_OK) return err;
    if ((err = mdns_service_add(PRISMA_APPLICATION, "_http", "_tcp", 80, NULL, 0)) != ESP_OK) return err;

    mdns_txt_item_t txt[] = {
        {"product", PRISMA_PRODUCT},
        {"application", PRISMA_APPLICATION},
        {"vendor", PRISMA_VENDOR},
        {"firmware", PRISMA_FIRMWARE_VERSION},
        {"path", "/camera-synth"}
    };
    err = mdns_service_txt_set("_http", "_tcp", txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) return err;

    s_state.ready = true;
    ESP_LOGI(TAG, "mDNS ready: %s", s_state.url);
    return ESP_OK;
}
