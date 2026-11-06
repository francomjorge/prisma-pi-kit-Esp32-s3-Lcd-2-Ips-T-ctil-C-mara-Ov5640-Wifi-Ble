#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "app.h"
#include "identity.h"

#define OTA_PROJECT_NAME "prisma_camera_synth"
#define OTA_CHUNK_SIZE 4096
#define OTA_HEADER_SIZE (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

static const char *TAG = "CS009_OTA";
static bool s_update_busy;

static esp_err_t json(httpd_req_t *req, const char *body)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}
static const char *ota_state_text(esp_ota_img_states_t state)
{
    switch (state) {
        case ESP_OTA_IMG_NEW: return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
        case ESP_OTA_IMG_VALID: return "valid";
        case ESP_OTA_IMG_INVALID: return "invalid";
        case ESP_OTA_IMG_ABORTED: return "aborted";
        case ESP_OTA_IMG_UNDEFINED: return "undefined";
        default: return "unknown";
    }
}
static bool update_access_allowed(void)
{
    prisma_settings_t cfg = {0};
    settings_get(&cfg);
    return auth_is_enabled() && !cfg.ap_open;
}
static bool header_is(httpd_req_t *req, const char *name, const char *expected)
{
    char value[72] = {0};
    return httpd_req_get_hdr_value_str(req, name, value, sizeof(value)) == ESP_OK &&
           strcmp(value, expected) == 0;
}
static bool decode_sha256(const char *hex, uint8_t value[32])
{
    if (!hex || strlen(hex) != 64) return false;
    for (unsigned i = 0; i < 32; i++) {
        int digits[2];
        for (int j = 0; j < 2; ++j) {
            char c = hex[i * 2 + j];
            if (c >= '0' && c <= '9') digits[j] = c - '0';
            else if (c >= 'a' && c <= 'f') digits[j] = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digits[j] = c - 'A' + 10;
            else return false;
        }
        value[i] = (uint8_t)((digits[0] << 4) | digits[1]);
    }
    return true;
}
static bool fixed_equal(const uint8_t *a, const uint8_t *b, size_t count)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < count; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}
static esp_err_t check_image_header(const uint8_t *buf, size_t size, char version[33])
{
    if (size < OTA_HEADER_SIZE) return ESP_ERR_INVALID_SIZE;
    const esp_image_header_t *image = (const esp_image_header_t *)buf;
    if (image->magic != ESP_IMAGE_HEADER_MAGIC) return ESP_ERR_INVALID_RESPONSE;
    esp_app_desc_t description = {0};
    memcpy(&description,
           buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
           sizeof(description));
    if (description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        strncmp(description.project_name, OTA_PROJECT_NAME,
                sizeof(description.project_name)) != 0)
        return ESP_ERR_INVALID_RESPONSE;
    const esp_app_desc_t *running = esp_app_get_description();
    if (running && strncmp(description.version, running->version,
                           sizeof(description.version)) == 0)
        return ESP_ERR_INVALID_VERSION;
    memcpy(version, description.version, 32);
    version[32] = '\0';
    return ESP_OK;
}
static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}
static bool schedule_restart(void)
{
    return xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 4, NULL) == pdPASS;
}
void ota_boot_check(bool core_ok)
{
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (core_ok) {
            esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
            if (e == ESP_OK) {
                platform_log_event("INFO", "ota", "Pending OTA confirmed: Admin+network+display ready");
                ESP_LOGI(TAG, "OTA boot confirmed on %s", running->label);
            } else {
                platform_log_event("ERROR", "ota", "OTA confirmation failed");
                ESP_LOGE(TAG, "OTA confirmation error: %s", esp_err_to_name(e));
            }
        } else {
            platform_log_event("ERROR", "ota", "First OTA boot failed core health; rollback requested");
            ESP_LOGE(TAG, "Core self-test failed on first OTA boot; requesting rollback");
            esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot();
            ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(e));
        }
    }
#else
    (void)core_ok;
#endif
}
static esp_err_t update_status_get(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    bool has_state = running && esp_ota_get_state_partition(running, &state) == ESP_OK;
    const esp_app_desc_t *desc = esp_app_get_description();
    bool allowed = update_access_allowed();
    char body[1000];
    int n = snprintf(body, sizeof(body),
        "{\"schema\":\"prisma.ota.v1\",\"enabled\":true,\"upload_allowed\":%s,"
        "\"reason\":\"%s\",\"busy\":%s,\"rollback_compiled\":%s,"
        "\"rollback_available\":%s,\"running_slot\":\"%s\",\"boot_slot\":\"%s\","
        "\"target_slot\":\"%s\",\"target_capacity_bytes\":%lu,"
        "\"running_version\":\"%s\",\"running_project\":\"%s\","
        "\"running_state\":\"%s\",\"transport\":\"HTTP_NO_TLS\","
        "\"image_type\":\"app_bin_only\",\"requires_sha256\":true,"
        "\"preserves_internal_and_sd\":true}",
        allowed ? "true" : "false",
        allowed ? "ready" : "set_admin_password_and_wpa2_ap",
        s_update_busy ? "true" : "false",
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        "true",
#else
        "false",
#endif
        esp_ota_check_rollback_is_possible() ? "true" : "false",
        running ? running->label : "none", boot ? boot->label : "none",
        target ? target->label : "none",
        target ? (unsigned long)target->size : 0UL,
        desc ? desc->version : "", desc ? desc->project_name : "",
        has_state ? ota_state_text(state) : "unknown");
    if (n < 0 || n >= (int)sizeof(body))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status overflow");
    return json(req, body);
}
static esp_err_t update_upload_post(httpd_req_t *req)
{
    if (!update_access_allowed())
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                   "Set Admin password and secure AP with WPA2 before OTA");
    if (!header_is(req, "X-PRISMA-OTA", "app"))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "app-only OTA header required");
    if (s_update_busy)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "update already running");

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA target");
    if (req->content_len < OTA_HEADER_SIZE + 64 || req->content_len > target->size)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "image does not fit OTA slot");

    char expected_hex[72] = {0};
    uint8_t expected[32], actual[32];
    if (httpd_req_get_hdr_value_str(req, "X-PRISMA-SHA256",
                                    expected_hex, sizeof(expected_hex)) != ESP_OK ||
        !decode_sha256(expected_hex, expected))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected SHA256 required");

    s_update_busy = true;
    uint8_t *chunk = malloc(OTA_CHUNK_SIZE);
    if (!chunk) {
        s_update_busy = false;
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    esp_ota_handle_t handle = 0;
    bool begun = false;
    esp_err_t error = ESP_OK;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts_ret(&sha, 0) != 0) error = ESP_FAIL;
    size_t received = 0, staged = 0;
    unsigned timeout_count = 0;
    char version[33] = {0};

    while (error == ESP_OK && received < req->content_len) {
        size_t want = (size_t)req->content_len - received;
        if (want > OTA_CHUNK_SIZE - staged) want = OTA_CHUNK_SIZE - staged;
        int n = httpd_req_recv(req, (char *)chunk + staged, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeout_count > 12) error = ESP_ERR_TIMEOUT;
            continue;
        }
        if (n <= 0) { error = ESP_FAIL; break; }
        timeout_count = 0;
        received += (size_t)n;
        staged += (size_t)n;

        /* Aggregate a full image descriptor before writing any flash. */
        if (!begun) {
            if (staged < OTA_HEADER_SIZE) continue;
            error = check_image_header(chunk, staged, version);
            if (error != ESP_OK) break;
            error = esp_ota_begin(target, (size_t)req->content_len, &handle);
            if (error != ESP_OK) break;
            begun = true;
        }
        if (mbedtls_sha256_update_ret(&sha, chunk, staged) != 0) {
            error = ESP_FAIL;
            break;
        }
        error = esp_ota_write(handle, chunk, staged);
        staged = 0;
    }
    if (error == ESP_OK && received != req->content_len) error = ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK && staged != 0) error = ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK && mbedtls_sha256_finish_ret(&sha, actual) != 0) error = ESP_FAIL;
    mbedtls_sha256_free(&sha);

    if (error == ESP_OK && !fixed_equal(expected, actual, sizeof(expected)))
        error = ESP_ERR_INVALID_CRC;

    if (begun) {
        if (error == ESP_OK) {
            esp_err_t end = esp_ota_end(handle);
            begun = false;
            if (end != ESP_OK) error = end;
        } else {
            esp_ota_abort(handle);
            begun = false;
        }
    }
    free(chunk);

    if (error == ESP_OK) error = esp_ota_set_boot_partition(target);
    if (error == ESP_OK) {
        char msg[112];
        snprintf(msg, sizeof(msg), "OTA staged: slot=%s version=%.32s", target->label, version);
        platform_log_event("INFO", "ota", msg);
        char response[320];
        snprintf(response, sizeof(response),
            "{\"ok\":true,\"slot\":\"%s\",\"version\":\"%.32s\","
            "\"sha256_verified\":true,\"rebooting\":true}", target->label, version);
        esp_err_t sent = json(req, response);
        (void)schedule_restart();
        s_update_busy = false;
        return sent;
    }

    s_update_busy = false;
    platform_log_event("WARN", "ota", "OTA rejected; running image preserved");
    ESP_LOGE(TAG, "OTA upload failed: %s", esp_err_to_name(error));
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                               "OTA rejected: image, integrity, transfer or flash validation failed");
}
static esp_err_t update_rollback_post(httpd_req_t *req)
{
    if (!update_access_allowed())
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                   "Admin password and WPA2 AP required");
    if (!header_is(req, "X-PRISMA-Confirm", "rollback"))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "rollback confirmation header required");
    if (s_update_busy)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "update running");
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    if (!esp_ota_check_rollback_is_possible())
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no verified previous OTA image");
    esp_err_t e = esp_ota_mark_app_invalid_rollback();
    if (e != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rollback denied by bootloader");
    platform_log_event("WARN", "ota", "Manual rollback requested; rebooting into previous valid image");
    esp_err_t sent = json(req, "{\"ok\":true,\"rebooting\":true,\"rollback\":true}");
    (void)schedule_restart();
    return sent;
#else
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bootloader rollback not compiled");
#endif
}
esp_err_t ota_web_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/api/v1/update/status", .method = HTTP_GET, .handler = update_status_get},
        {.uri = "/api/v1/update/upload", .method = HTTP_POST, .handler = update_upload_post},
        {.uri = "/api/v1/update/rollback", .method = HTTP_POST, .handler = update_rollback_post}
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t e = auth_register_uri_handler(server, &routes[i]);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}
