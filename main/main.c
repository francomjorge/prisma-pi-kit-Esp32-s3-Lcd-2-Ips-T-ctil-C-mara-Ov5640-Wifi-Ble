#include "app.h"
#include "identity.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "CS002";

static bool init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) return false;
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s / %s starting", PRISMA_PRODUCT, PRISMA_BUILD_ID);
    if (!init_nvs()) ESP_LOGE(TAG, "NVS initialization failed; continuing in degraded mode");

    esp_err_t err = display_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(err));

    err = network_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network init failed: %s", esp_err_to_name(err));
        return;
    }

    err = discovery_start();
    if (err != ESP_OK) ESP_LOGW(TAG, "mDNS init failed; IP access remains available: %s", esp_err_to_name(err));

    err = web_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed: %s", esp_err_to_name(err));
        return;
    }

    network_state_t net = {0};
    discovery_state_t discovery = {0};
    network_get_state(&net);
    discovery_get_state(&discovery);
    ESP_LOGI(TAG, "READY display=%s AP=%s IP=%s HTTP=80 mDNS=%s",
             display_ready() ? "ready" : "error",
             net.ssid[0] ? net.ssid : "-",
             net.ip[0] ? net.ip : "-",
             discovery.ready ? discovery.url : "unavailable");
}
