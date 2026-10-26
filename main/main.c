#include "app.h"
#include "identity.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "CS003";

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

    err = touch_hw_probe();
    if (err != ESP_OK) ESP_LOGW(TAG, "Touch probe failed; continuing: %s", esp_err_to_name(err));

    err = camera_hw_probe();
    if (err != ESP_OK) ESP_LOGW(TAG, "Camera probe failed; continuing without capture: %s", esp_err_to_name(err));

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
    camera_hw_state_t camera = {0};
    touch_hw_state_t touch = {0};
    network_get_state(&net);
    discovery_get_state(&discovery);
    camera_hw_get_state(&camera);
    touch_hw_get_state(&touch);

    ESP_LOGI(TAG, "READY display=%s camera=%s PID=%04X touch=%s ID=%02X AP=%s IP=%s mDNS=%s",
             display_ready() ? "ready" : "error",
             camera.ready ? camera.sensor_name : "error",
             camera.pid,
             touch.ready ? touch.controller : "error",
             touch.chip_id,
             net.ssid[0] ? net.ssid : "-",
             net.ip[0] ? net.ip : "-",
             discovery.ready ? discovery.url : "unavailable");
}
