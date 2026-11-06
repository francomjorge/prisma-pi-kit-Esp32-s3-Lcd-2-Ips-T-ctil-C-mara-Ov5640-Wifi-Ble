#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "cJSON.h"
#include "app.h"

#define AUTH_MAGIC 0x50415337U
#define AUTH_ROUNDS 12000U
#define AUTH_ROUTES_MAX 48

typedef struct {
    uint32_t magic;
    uint32_t rounds;
    uint8_t salt[16];
    uint8_t hash[32];
} auth_record_t;

typedef struct {
    esp_err_t (*handler)(httpd_req_t *);
    void *original_context;
} auth_route_t;

static const char *TAG = "CS007_SECURITY";
static const char *NS = "prisma_auth";
static const char *KEY = "admin_v1";
static auth_record_t s_record;
static bool s_enabled;
static bool s_locked;
static auth_route_t s_routes[AUTH_ROUTES_MAX];
static size_t s_route_count;

bool auth_is_enabled(void) { return s_enabled; }
const char *auth_state(void) { return s_locked ? "credential_error" : (s_enabled ? "enabled" : "not_configured"); }

static bool fixed_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

/* PBKDF2-HMAC-SHA256, one 32-byte block. Avoids storing plaintext credentials. */
static bool derive(const char *password, const uint8_t salt[16], uint32_t rounds, uint8_t out[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md || rounds < 1000 || rounds > 100000) return false;
    uint8_t block[20], u[32], next[32];
    memcpy(block, salt, 16);
    block[16] = block[17] = block[18] = 0;
    block[19] = 1;
    const unsigned char *pw = (const unsigned char *)password;
    size_t pw_len = strlen(password);
    if (mbedtls_md_hmac(md, pw, pw_len, block, sizeof(block), u) != 0) return false;
    memcpy(out, u, sizeof(u));
    for (uint32_t i = 1; i < rounds; ++i) {
        if (mbedtls_md_hmac(md, pw, pw_len, u, sizeof(u), next) != 0) return false;
        for (size_t j = 0; j < sizeof(u); ++j) out[j] ^= next[j];
        memcpy(u, next, sizeof(u));
    }
    memset(block, 0, sizeof(block));
    memset(u, 0, sizeof(u));
    memset(next, 0, sizeof(next));
    return true;
}

static bool password_valid(const char *password)
{
    if (!password) return false;
    size_t n = strlen(password);
    if (n < 12 || n > 63) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)password[i];
        if (c < 33 || c > 126) return false;
    }
    return true;
}

static bool verify_password(const char *password)
{
    if (!s_enabled || s_locked || !password) return false;
    uint8_t result[32] = {0};
    bool ok = derive(password, s_record.salt, s_record.rounds, result) &&
              fixed_equal(result, s_record.hash, sizeof(result));
    memset(result, 0, sizeof(result));
    return ok;
}

esp_err_t auth_start(void)
{
    memset(&s_record, 0, sizeof(s_record));
    s_enabled = false;
    s_locked = false;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Admin password not configured; HTTP admin is OPEN");
        return ESP_OK;
    }
    if (e != ESP_OK) { s_locked = true; return e; }
    size_t size = sizeof(s_record);
    e = nvs_get_blob(h, KEY, &s_record, &size);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Admin password not configured; HTTP admin is OPEN");
        return ESP_OK;
    }
    if (e != ESP_OK || size != sizeof(s_record) ||
        s_record.magic != AUTH_MAGIC || s_record.rounds != AUTH_ROUNDS) {
        s_locked = true;
        ESP_LOGE(TAG, "Admin credential storage invalid; protected routes locked");
        return ESP_FAIL;
    }
    s_enabled = true;
    ESP_LOGI(TAG, "Admin HTTP Basic authentication enabled (HTTP transport, no TLS)");
    return ESP_OK;
}

esp_err_t auth_set_password(const char *current, const char *next_password)
{
    if (s_locked || !password_valid(next_password)) return ESP_ERR_INVALID_ARG;
    if (s_enabled && !verify_password(current)) return ESP_ERR_INVALID_STATE;

    auth_record_t record = { .magic = AUTH_MAGIC, .rounds = AUTH_ROUNDS };
    esp_fill_random(record.salt, sizeof(record.salt));
    if (!derive(next_password, record.salt, record.rounds, record.hash)) return ESP_FAIL;

    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, KEY, &record, sizeof(record));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) {
        s_record = record;
        s_enabled = true;
        platform_log_event("INFO", "security", "Admin credentials configured/changed");
    }
    memset(&record, 0, sizeof(record));
    return e;
}

esp_err_t auth_factory_reset(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) return e;
    if (e == ESP_OK) {
        e = nvs_erase_key(h, KEY);
        if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    } else e = ESP_OK;
    if (e == ESP_OK) {
        memset(&s_record, 0, sizeof(s_record));
        s_enabled = false;
        s_locked = false;
        platform_log_event("WARN", "security", "Admin credentials factory-reset");
    }
    return e;
}

static bool basic_authorized(httpd_req_t *req)
{
    char header[200];
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK)
        return false;
    if (strncmp(header, "Basic ", 6) != 0) return false;

    uint8_t decoded[128] = {0};
    size_t n = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &n,
                              (const unsigned char *)header + 6,
                              strlen(header + 6)) != 0) return false;
    decoded[n] = 0;
    char *sep = strchr((char *)decoded, ':');
    if (!sep) return false;
    *sep++ = 0;
    bool user_ok = strcmp((char *)decoded, "admin") == 0;
    bool password_ok = verify_password(sep);
    memset(decoded, 0, sizeof(decoded));
    memset(header, 0, sizeof(header));
    return user_ok && password_ok;
}

static esp_err_t deny_login(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate",
                       "Basic realm=\"PRISMA-PI CAMERA SYNTH\", charset=\"UTF-8\"");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "Admin authentication required");
}

static bool public_route(const httpd_req_t *req)
{
    if (req->method != HTTP_GET) return false;
    const char *uri = req->uri;
    return !strcmp(uri, "/") || !strcmp(uri, "/camera-synth") ||
           !strcmp(uri, "/camera-sync") || !strcmp(uri, "/shared.css") ||
           !strcmp(uri, "/api/v1/capabilities") ||
           !strcmp(uri, "/api/v1/security");
}

static esp_err_t guarded_handler(httpd_req_t *req)
{
    auth_route_t *route = (auth_route_t *)req->user_ctx;
    if (!route || !route->handler) return ESP_FAIL;

    if (s_locked && !public_route(req))
        return httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "admin credential error");

    /* Sensitive backup must never be downloadable while admin authentication is OFF. */
    if (!s_enabled && req->method == HTTP_GET &&
        !strncmp(req->uri, "/api/v1/config/export", 21)) {
        char query[128] = {0};
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
            strstr(query, "include_secrets") != NULL)
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                       "Configure Admin password before exporting secrets");
    }

    if (s_enabled && !public_route(req) && !basic_authorized(req))
        return deny_login(req);

    /* Non-simple header blocks ordinary browser cross-site form POSTs (CSRF). */
    if (req->method == HTTP_POST) {
        char value[12];
        if (httpd_req_get_hdr_value_str(req, "X-PRISMA-Action", value, sizeof(value)) != ESP_OK ||
            strcmp(value, "1") != 0)
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "X-PRISMA-Action required");
    }

    req->user_ctx = route->original_context;
    esp_err_t e = route->handler(req);
    req->user_ctx = route;
    return e;
}

esp_err_t auth_register_uri_handler(httpd_handle_t server, const httpd_uri_t *uri)
{
    if (!server || !uri || s_route_count >= AUTH_ROUTES_MAX) return ESP_ERR_NO_MEM;
    auth_route_t *slot = &s_routes[s_route_count];
    slot->handler = uri->handler;
    slot->original_context = uri->user_ctx;
    httpd_uri_t guarded = *uri;
    guarded.handler = guarded_handler;
    guarded.user_ctx = slot;
    esp_err_t e = httpd_register_uri_handler(server, &guarded);
    if (e == ESP_OK) ++s_route_count;
    return e;
}

static esp_err_t security_get(httpd_req_t *req)
{
    prisma_settings_t settings = {0};
    settings_get(&settings);
    char json[400];
    snprintf(json, sizeof(json),
             "{\"schema\":\"prisma.security.v1\",\"admin_auth\":\"%s\","
             "\"enabled\":%s,\"username\":\"admin\",\"method\":\"HTTP_BASIC\","
             "\"password_min_length\":12,\"http_tls\":false,\"ap_open\":%s,"
             "\"credentials_in_backup\":false}",
             auth_state(), s_enabled ? "true" : "false",
             settings.ap_open ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t security_post(httpd_req_t *req)
{
    if (req->content_len < 2 || req->content_len > 300)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid content size");
    char payload[301] = {0};
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, payload + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        got += (size_t)n;
    }
    cJSON *root = cJSON_Parse(payload);
    memset(payload, 0, sizeof(payload));
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    const cJSON *old_item = cJSON_GetObjectItemCaseSensitive(root, "current_password");
    const cJSON *new_item = cJSON_GetObjectItemCaseSensitive(root, "new_password");
    const char *old_value = cJSON_IsString(old_item) ? old_item->valuestring : "";
    const char *new_value = cJSON_IsString(new_item) ? new_item->valuestring : "";
    esp_err_t e = auth_set_password(old_value, new_value);
    cJSON_Delete(root);
    if (e != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "password rejected (12-63 ASCII non-space; check current password)");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req,
         "{\"ok\":true,\"enabled\":true,\"method\":\"HTTP_BASIC\",\"reload_required\":true}");
}

esp_err_t auth_web_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri="/api/v1/security", .method=HTTP_GET, .handler=security_get},
        {.uri="/api/v1/security", .method=HTTP_POST, .handler=security_post}
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t e = auth_register_uri_handler(server, &routes[i]);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}
