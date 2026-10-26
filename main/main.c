#include <stdio.h>
#include "app.h"
#include "identity.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "CS006";

static bool init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) return false;
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

static void log_result(const char *subsystem, esp_err_t err, const char *ok_message)
{
    char msg[112];
    if (err == ESP_OK) {
        platform_log_event("INFO", subsystem, ok_message);
    } else {
        snprintf(msg, sizeof(msg), "%s: %s", ok_message, esp_err_to_name(err));
        platform_log_event("WARN", subsystem, msg);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s / %s starting", PRISMA_PRODUCT, PRISMA_BUILD_ID);
    platform_log_start();
    platform_log_event("INFO", "boot", "Application startup");

    bool nvs_ok = init_nvs();
    log_result("nvs", nvs_ok ? ESP_OK : ESP_FAIL, "NVS initialized");

    esp_err_t err = settings_start();
    log_result("settings", err, "Settings loaded");

    /* SD shares SPI2 with LCD. Storage runs first so the card enters SPI mode before LCD traffic. */
    err = storage_start();
    log_result("storage", err, "Storage initialized");

    err = display_start();
    log_result("display", err, "Display initialized");

    err = board_i2c_start();
    log_result("i2c", err, "Shared I2C bus initialized");

    err = touch_hw_probe();
    log_result("touch", err, "Touch hardware probed");

    err = imu_hw_probe();
    log_result("imu", err, "QMI8658 hardware probed");

    err = camera_hw_probe();
    log_result("camera", err, "Camera hardware probed without capture");

    err = network_start();
    log_result("network", err, "Network initialized");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network init failed: %s", esp_err_to_name(err));
        return;
    }

    err = discovery_start();
    log_result("mdns", err, "mDNS initialized");

    err = web_start();
    log_result("web", err, "HTTP administration started");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed: %s", esp_err_to_name(err));
        return;
    }

    network_state_t net = {0};
    discovery_state_t discovery = {0};
    camera_hw_state_t camera = {0};
    touch_hw_state_t touch = {0};
    imu_hw_state_t imu = {0};
    storage_state_t storage = {0};
    network_get_state(&net);
    discovery_get_state(&discovery);
    camera_hw_get_state(&camera);
    touch_hw_get_state(&touch);
    imu_hw_get_state(&imu);
    storage_get_state(&storage);

    ESP_LOGI(TAG,
             "READY camera=%s PID=%04X touch=%s IMU=%s@0x%02X internal=%s sd=%s AP=%s IP=%s STA=%s mDNS=%s",
             camera.ready ? camera.sensor_name : "error", camera.pid,
             touch.ready ? touch.controller : "error",
             imu.ready ? imu.controller : "error", imu.address,
             storage.internal.mounted ? "mounted" : "unavailable",
             storage.sd.mounted ? "mounted" : "unavailable",
             net.ssid[0] ? net.ssid : "-", net.ip[0] ? net.ip : "-",
             net.sta_connected ? net.sta_ip : "disconnected",
             discovery.ready ? discovery.url : "unavailable");
    platform_log_event("INFO", "boot", "Startup sequence complete");
}
