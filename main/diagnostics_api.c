#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "app.h"
#include "identity.h"

/* CS008. Read-only support bundle; intentionally no camera frames or filesystem mutation. */
static void set_string(cJSON *parent, const char *key, const char *value)
{
    cJSON_AddStringToObject(parent, key, value ? value : "");
}
static void set_bool(cJSON *parent, const char *key, bool value)
{
    cJSON_AddBoolToObject(parent, key, value);
}
static void set_u64(cJSON *parent, const char *key, uint64_t value)
{
    cJSON_AddNumberToObject(parent, key, (double)value);
}
static void add_volume(cJSON *root, const char *key, const storage_volume_state_t *v)
{
    cJSON *o = cJSON_AddObjectToObject(root, key);
    set_bool(o, "mounted", v->mounted);
    set_bool(o, "available", v->available);
    set_bool(o, "writable", v->writable && v->mounted);
    set_string(o, "filesystem", v->fs);
    set_u64(o, "total_bytes", v->total_bytes);
    set_u64(o, "used_bytes", v->used_bytes);
    set_u64(o, "free_bytes", v->free_bytes);
    set_u64(o, "max_simultaneously_open_files", v->max_open_files);
    set_string(o, "error", v->error);
}
static esp_err_t send_object(httpd_req_t *req, cJSON *root, bool download)
{
    if (!root) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "serialization failed");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    if (download)
        httpd_resp_set_hdr(req, "Content-Disposition",
                           "attachment; filename=\"prisma-camera-synth-cs008-support.json\"");
    esp_err_t e = httpd_resp_sendstr(req, json);
    free(json);
    return e;
}
static cJSON *collect(bool include_events)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *identity = cJSON_AddObjectToObject(root, "identity");
    set_string(root, "schema", "prisma.support.v1");
    set_string(identity, "product", PRISMA_PRODUCT);
    set_string(identity, "firmware", PRISMA_FIRMWARE_VERSION);
    set_string(identity, "build", PRISMA_BUILD_ID);
    set_string(identity, "vendor", PRISMA_VENDOR);
    set_string(root, "capture_policy", "camera_inventory_only; no_capture; no_stream");
    set_string(root, "secrets_policy", "wifi_credentials_and_admin_password_omitted");
    set_string(root, "event_log_scope", "last_64_events_since_boot_in_ram");
    set_u64(root, "uptime_ms", (uint64_t)(esp_timer_get_time() / 1000));
    set_u64(root, "reset_reason_code", (unsigned)esp_reset_reason());

    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    bool flash_read_ok = esp_flash_get_size(NULL, &flash_size) == ESP_OK;
    cJSON *board = cJSON_AddObjectToObject(root, "board");
    set_string(board, "chip", "ESP32-S3");
    set_u64(board, "revision", (unsigned)chip.revision);
    set_u64(board, "cores", (unsigned)chip.cores);
    set_bool(board, "flash_detected", flash_read_ok);
    set_u64(board, "flash_bytes", flash_size);

    cJSON *mem = cJSON_AddObjectToObject(root, "memory");
    set_u64(mem, "heap_free_bytes", esp_get_free_heap_size());
    set_u64(mem, "heap_minimum_bytes", esp_get_minimum_free_heap_size());
    set_u64(mem, "heap_largest_block", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    set_u64(mem, "psram_total_bytes", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    set_u64(mem, "psram_free_bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    set_u64(mem, "psram_largest_block", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    network_state_t net = {0};
    discovery_state_t discovery = {0};
    storage_state_t storage = {0};
    camera_hw_state_t camera = {0};
    touch_hw_state_t touch = {0};
    imu_hw_state_t imu = {0};
    network_get_state(&net);
    discovery_get_state(&discovery);
    storage_get_state(&storage);
    camera_hw_get_state(&camera);
    touch_hw_get_state(&touch);
    imu_hw_get_state(&imu);

    cJSON *network = cJSON_AddObjectToObject(root, "network");
    set_bool(network, "ap_ready", net.ready);
    set_bool(network, "ap_open", net.open);
    set_u64(network, "ap_clients", net.clients);
    set_u64(network, "ap_channel", net.channel);
    set_bool(network, "sta_configured", net.sta_configured);
    set_bool(network, "sta_connected", net.sta_connected);
    set_bool(network, "mdns_ready", discovery.ready);
    /* No network SSIDs, MACs, IPs, plaintext passwords or admin secrets. */

    cJSON *storage_json = cJSON_AddObjectToObject(root, "storage");
    set_bool(storage_json, "shared_spi_ready", storage.shared_spi_ready);
    add_volume(storage_json, "internal", &storage.internal);
    add_volume(storage_json, "sd", &storage.sd);
    set_bool(storage_json, "ota_slot_0_present",
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL) != NULL);
    set_bool(storage_json, "ota_slot_1_present",
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL) != NULL);

    cJSON *hw = cJSON_AddObjectToObject(root, "hardware");
    set_bool(hw, "display_ready", display_ready());
    set_bool(hw, "i2c_ready", board_i2c_ready());
    cJSON *touch_json = cJSON_AddObjectToObject(hw, "touch");
    set_bool(touch_json, "ready", touch.ready);
    set_string(touch_json, "controller", touch.controller);
    set_u64(touch_json, "address", touch.address);
    set_u64(touch_json, "chip_id", touch.chip_id);
    set_string(touch_json, "error", touch.error);

    cJSON *imu_json = cJSON_AddObjectToObject(hw, "imu");
    set_bool(imu_json, "ready", imu.ready);
    set_string(imu_json, "controller", imu.controller);
    set_u64(imu_json, "address", imu.address);
    set_u64(imu_json, "who_am_i", imu.who_am_i);
    set_u64(imu_json, "revision", imu.revision);
    set_string(imu_json, "error", imu.error);

    cJSON *cam_json = cJSON_AddObjectToObject(hw, "camera_inventory");
    set_bool(cam_json, "ready", camera.ready);
    set_bool(cam_json, "expected_sensor", camera.expected);
    set_string(cam_json, "sensor", camera.sensor_name);
    set_u64(cam_json, "pid", camera.pid);
    set_string(cam_json, "error", camera.error);
    set_bool(cam_json, "capture_enabled", false);

    cJSON *security = cJSON_AddObjectToObject(root, "security");
    set_string(security, "admin_auth_state", auth_state());
    set_bool(security, "http_tls", false);
    set_bool(security, "administration_over_http", true);

    set_u64(root, "total_events_since_boot", platform_log_total_events());
    if (include_events) {
        platform_event_t *events = calloc(PRISMA_EVENT_LOG_CAPACITY, sizeof(*events));
        size_t count = events ? platform_log_copy(events, PRISMA_EVENT_LOG_CAPACITY) : 0;
        cJSON *list = cJSON_AddArrayToObject(root, "recent_events");
        for (size_t i = 0; i < count; ++i) {
            cJSON *event = cJSON_CreateObject();
            if (!event) break;
            set_u64(event, "sequence", events[i].sequence);
            set_u64(event, "uptime_ms", events[i].uptime_ms);
            set_string(event, "level", events[i].level);
            set_string(event, "subsystem", events[i].subsystem);
            set_string(event, "message", events[i].message);
            cJSON_AddItemToArray(list, event);
        }
        free(events);
    }
    return root;
}
static esp_err_t diagnostics_report_get(httpd_req_t *req)
{
    return send_object(req, collect(true), true);
}
static esp_err_t diagnostics_check_get(httpd_req_t *req)
{
    network_state_t net = {0};
    storage_state_t st = {0};
    discovery_state_t discovery = {0};
    camera_hw_state_t cam = {0};
    touch_hw_state_t touch = {0};
    imu_hw_state_t imu = {0};
    network_get_state(&net);
    storage_get_state(&st);
    discovery_get_state(&discovery);
    camera_hw_get_state(&cam);
    touch_hw_get_state(&touch);
    imu_hw_get_state(&imu);

    bool display = display_ready();
    bool core_ok = net.ready && display;
    bool storage_ok = st.internal.mounted || st.sd.mounted;
    bool hardware_ok = cam.ready && touch.ready && imu.ready;
    bool warn = !storage_ok || !hardware_ok || !discovery.ready ||
                (net.sta_configured && !net.sta_connected);

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    set_string(root, "schema", "prisma.diagnostics.check.v1");
    set_string(root, "result", !core_ok ? "error" : warn ? "warning" : "pass");
    set_string(root, "mode", "read_only_cached_hardware_state");
    set_string(root, "note", "No filesystem write, no bus probe, no camera frame");
    cJSON *checks = cJSON_AddObjectToObject(root, "checks");
    set_bool(checks, "display", display);
    set_bool(checks, "softap", net.ready);
    set_bool(checks, "sta_connected", net.sta_connected);
    set_bool(checks, "mdns", discovery.ready);
    set_bool(checks, "shared_spi", st.shared_spi_ready);
    set_bool(checks, "internal_mounted", st.internal.mounted);
    set_bool(checks, "sd_mounted", st.sd.mounted);
    set_bool(checks, "i2c", board_i2c_ready());
    set_bool(checks, "touch_probe", touch.ready);
    set_bool(checks, "imu_probe", imu.ready);
    set_bool(checks, "camera_inventory", cam.ready);
    set_bool(checks, "admin_auth", auth_is_enabled());
    set_u64(root, "heap_minimum_bytes", esp_get_minimum_free_heap_size());
    set_u64(root, "uptime_ms", (uint64_t)(esp_timer_get_time() / 1000));
    return send_object(req, root, false);
}
esp_err_t diagnostics_web_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/api/v1/diagnostics/check", .method = HTTP_GET, .handler = diagnostics_check_get},
        {.uri = "/api/v1/diagnostics/report", .method = HTTP_GET, .handler = diagnostics_report_get}
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) {
        esp_err_t e = auth_register_uri_handler(server, &routes[i]);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}
