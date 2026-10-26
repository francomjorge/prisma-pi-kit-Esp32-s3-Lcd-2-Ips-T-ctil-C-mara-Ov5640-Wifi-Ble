#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "app.h"

#define LOGICAL_PATH_MAX 220
#define QUERY_MAX 640
#define IO_BUF_SIZE 4096

static const char *TAG = "CS005_FILES";

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s)
{
    char *r = s;
    char *w = s;
    while (*r) {
        if (*r == '+') {
            *w++ = ' ';
            ++r;
        } else if (*r == '%' && r[1] && r[2]) {
            int a = hex_value(r[1]);
            int b = hex_value(r[2]);
            if (a >= 0 && b >= 0) {
                *w++ = (char)((a << 4) | b);
                r += 3;
            } else {
                *w++ = *r++;
            }
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

static void json_escape(const char *src, char *dst, size_t cap)
{
    size_t j = 0;
    if (cap == 0) return;
    for (size_t i = 0; src && src[i] && j + 2 < cap; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c == '\r') {
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else if (c == '\t') {
            dst[j++] = '\\';
            dst[j++] = 't';
        } else if (c >= 32) {
            dst[j++] = (char)c;
        }
    }
    dst[j] = 0;
}

static bool logical_path_valid(const char *path)
{
    if (!path || path[0] != '/' || strlen(path) > LOGICAL_PATH_MAX) return false;
    if (strchr(path, '\\')) return false;

    const char *p = path;
    while (*p) {
        if ((unsigned char)*p < 32) return false;
        ++p;
    }

    p = path;
    while (*p) {
        while (*p == '/') ++p;
        const char *start = p;
        while (*p && *p != '/') ++p;
        size_t len = (size_t)(p - start);
        if (len == 2 && start[0] == '.' && start[1] == '.') return false;
    }
    return true;
}

static esp_err_t resolve_path(const char *volume, const char *logical,
                              char *out, size_t cap)
{
    if (!logical_path_valid(logical)) return ESP_ERR_INVALID_ARG;
    const char *root = storage_volume_root(volume);
    if (!root) return ESP_ERR_INVALID_STATE;
    int n = snprintf(out, cap, "%s%s", root, logical);
    return n > 0 && (size_t)n < cap ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static bool query_value(httpd_req_t *req, const char *key,
                        char *out, size_t cap)
{
    size_t len = httpd_req_get_url_query_len(req);
    if (len == 0 || len >= QUERY_MAX) return false;
    char query[QUERY_MAX];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    if (httpd_query_key_value(query, key, out, cap) != ESP_OK) return false;
    url_decode(out);
    return true;
}

static esp_err_t read_form(httpd_req_t *req, char *buf, size_t cap)
{
    if (req->content_len == 0 || req->content_len >= cap) return ESP_ERR_INVALID_SIZE;
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, buf + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        received += (size_t)n;
    }
    buf[received] = 0;
    return ESP_OK;
}

static bool form_value(const char *body, const char *key,
                       char *out, size_t cap)
{
    if (httpd_query_key_value(body, key, out, cap) != ESP_OK) return false;
    url_decode(out);
    return true;
}

static const storage_volume_state_t *volume_state(const storage_state_t *state,
                                                   const char *volume)
{
    if (!strcmp(volume, "internal")) return &state->internal;
    if (!strcmp(volume, "sd")) return &state->sd;
    return NULL;
}

static esp_err_t json_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t storage_get(httpd_req_t *req)
{
    storage_state_t s = {0};
    storage_get_state(&s);
    char int_err[180];
    char sd_err[180];
    json_escape(s.internal.error, int_err, sizeof(int_err));
    json_escape(s.sd.error, sd_err, sizeof(sd_err));

    char json[2200];
    int n = snprintf(
        json, sizeof(json),
        "{\"schema\":\"prisma.storage.v1\","
        "\"shared_spi_ready\":%s,"
        "\"policy\":{\"file_count_quota\":false,\"directory_entry_limit\":false,"
        "\"max_open_files_per_volume\":%u,\"auto_format\":false},"
        "\"internal\":{\"available\":%s,\"mounted\":%s,\"writable\":%s,"
        "\"fs\":\"%s\",\"mount\":\"%s\",\"total_bytes\":%llu,"
        "\"used_bytes\":%llu,\"free_bytes\":%llu,\"error\":\"%s\"},"
        "\"sd\":{\"available\":%s,\"mounted\":%s,\"writable\":%s,"
        "\"fs\":\"%s\",\"mount\":\"%s\",\"total_bytes\":%llu,"
        "\"used_bytes\":%llu,\"free_bytes\":%llu,"
        "\"pins\":{\"sclk\":39,\"mosi\":38,\"miso\":40,\"cs\":41},"
        "\"error\":\"%s\"}}",
        s.shared_spi_ready ? "true" : "false",
        (unsigned)PRISMA_STORAGE_MAX_OPEN_FILES,
        s.internal.available ? "true" : "false",
        s.internal.mounted ? "true" : "false",
        s.internal.writable ? "true" : "false",
        s.internal.fs, s.internal.mount,
        (unsigned long long)s.internal.total_bytes,
        (unsigned long long)s.internal.used_bytes,
        (unsigned long long)s.internal.free_bytes, int_err,
        s.sd.available ? "true" : "false",
        s.sd.mounted ? "true" : "false",
        s.sd.writable ? "true" : "false",
        s.sd.fs, s.sd.mount,
        (unsigned long long)s.sd.total_bytes,
        (unsigned long long)s.sd.used_bytes,
        (unsigned long long)s.sd.free_bytes, sd_err);
    if (n < 0 || n >= (int)sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "storage status overflow");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, n);
}

static esp_err_t list_get(httpd_req_t *req)
{
    char volume[16] = {0};
    char logical[LOGICAL_PATH_MAX + 1] = {0};
    char fs_path[256];
    if (!query_value(req, "volume", volume, sizeof(volume))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "volume required");
    }
    if (!query_value(req, "path", logical, sizeof(logical))) {
        strlcpy(logical, "/", sizeof(logical));
    }
    if (resolve_path(volume, logical, fs_path, sizeof(fs_path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "invalid path or unmounted volume");
    }
    if (!storage_lock(3000)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "filesystem busy");
    }

    DIR *dir = opendir(fs_path);
    if (!dir) {
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "directory not found");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char volume_json[40];
    char path_json[480];
    json_escape(volume, volume_json, sizeof(volume_json));
    json_escape(logical, path_json, sizeof(path_json));
    char head[600];
    snprintf(head, sizeof(head),
             "{\"volume\":\"%s\",\"path\":\"%s\",\"entries\":[",
             volume_json, path_json);
    esp_err_t send_err = httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

    bool first = true;
    struct dirent *entry;
    while (send_err == ESP_OK && (entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        char child[512];
        size_t base_len = strlen(fs_path);
        const char *slash = base_len && fs_path[base_len - 1] == '/' ? "" : "/";
        int child_len = snprintf(child, sizeof(child), "%s%s%s",
                                 fs_path, slash, entry->d_name);
        if (child_len <= 0 || child_len >= (int)sizeof(child)) continue;

        struct stat st = {0};
        bool have_stat = stat(child, &st) == 0;
        bool is_dir = have_stat && S_ISDIR(st.st_mode);
        char name_json[600];
        json_escape(entry->d_name, name_json, sizeof(name_json));
        char item[820];
        int item_len = snprintf(
            item, sizeof(item),
            "%s{\"name\":\"%s\",\"type\":\"%s\",\"size\":%llu,\"mtime\":%lld}",
            first ? "" : ",", name_json, is_dir ? "dir" : "file",
            (unsigned long long)(have_stat && !is_dir ? (uint64_t)st.st_size : 0),
            (long long)(have_stat ? st.st_mtime : 0));
        if (item_len <= 0 || item_len >= (int)sizeof(item)) continue;
        send_err = httpd_resp_send_chunk(req, item, item_len);
        first = false;
    }

    closedir(dir);
    storage_unlock();
    if (send_err != ESP_OK) return send_err;
    send_err = httpd_resp_send_chunk(req, "]}", 2);
    if (send_err != ESP_OK) return send_err;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(dot, ".png")) return "image/png";
    if (!strcasecmp(dot, ".gif")) return "image/gif";
    if (!strcasecmp(dot, ".webp")) return "image/webp";
    if (!strcasecmp(dot, ".bmp")) return "image/bmp";
    if (!strcasecmp(dot, ".json")) return "application/json";
    if (!strcasecmp(dot, ".html") || !strcasecmp(dot, ".htm")) return "text/html; charset=utf-8";
    if (!strcasecmp(dot, ".css")) return "text/css; charset=utf-8";
    if (!strcasecmp(dot, ".js")) return "application/javascript; charset=utf-8";
    if (!strcasecmp(dot, ".txt") || !strcasecmp(dot, ".log") ||
        !strcasecmp(dot, ".md") || !strcasecmp(dot, ".csv") ||
        !strcasecmp(dot, ".ini") || !strcasecmp(dot, ".cfg") ||
        !strcasecmp(dot, ".yaml") || !strcasecmp(dot, ".yml")) {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

static void safe_filename(const char *path, char out[96])
{
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    size_t j = 0;
    for (size_t i = 0; name[i] && j < 94; ++i) {
        if (name[i] != '"' && name[i] != '\r' && name[i] != '\n') {
            out[j++] = name[i];
        }
    }
    out[j] = 0;
}

static esp_err_t download_get(httpd_req_t *req)
{
    char volume[16] = {0};
    char logical[LOGICAL_PATH_MAX + 1] = {0};
    char inline_value[8] = {0};
    char fs_path[256];
    if (!query_value(req, "volume", volume, sizeof(volume)) ||
        !query_value(req, "path", logical, sizeof(logical))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "volume/path required");
    }
    if (resolve_path(volume, logical, fs_path, sizeof(fs_path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "invalid path or unmounted volume");
    }
    query_value(req, "inline", inline_value, sizeof(inline_value));
    if (!storage_lock(3000)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "filesystem busy");
    }

    FILE *file = fopen(fs_path, "rb");
    if (!file) {
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
    }

    httpd_resp_set_type(req, mime_for(fs_path));
    char filename[96];
    char disposition[160];
    safe_filename(logical, filename);
    snprintf(disposition, sizeof(disposition), "%s; filename=\"%s\"",
             inline_value[0] == '1' ? "inline" : "attachment", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char buffer[IO_BUF_SIZE];
    size_t count;
    esp_err_t e = ESP_OK;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        e = httpd_resp_send_chunk(req, buffer, count);
        if (e != ESP_OK) break;
    }
    fclose(file);
    storage_unlock();
    if (e == ESP_OK) e = httpd_resp_send_chunk(req, NULL, 0);
    return e;
}

static esp_err_t upload_post(httpd_req_t *req)
{
    char volume[16] = {0};
    char logical[LOGICAL_PATH_MAX + 1] = {0};
    char dest[256];
    char temp[300];
    if (!query_value(req, "volume", volume, sizeof(volume)) ||
        !query_value(req, "path", logical, sizeof(logical))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "volume/path required");
    }
    if (resolve_path(volume, logical, dest, sizeof(dest)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "invalid path or unmounted volume");
    }

    storage_state_t storage = {0};
    storage_get_state(&storage);
    const storage_volume_state_t *vol = volume_state(&storage, volume);
    if (!vol || !vol->mounted) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "volume not mounted");
    }
    if ((uint64_t)req->content_len > vol->free_bytes) {
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req,
            "not enough free space for atomic upload; old file is preserved until completion");
    }
    if (!storage_lock(5000)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "filesystem busy");
    }

    struct stat existing = {0};
    if (stat(dest, &existing) == 0 && S_ISDIR(existing.st_mode)) {
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "destination is directory");
    }

    snprintf(temp, sizeof(temp), "%s.prisma-%08lx.part",
             dest, (unsigned long)esp_random());
    FILE *file = fopen(temp, "wb");
    if (!file) {
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "cannot create temporary file");
    }

    char buffer[IO_BUF_SIZE];
    size_t remaining = req->content_len;
    bool ok = true;
    while (remaining > 0) {
        size_t want = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        int n = httpd_req_recv(req, buffer, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0 || fwrite(buffer, 1, (size_t)n, file) != (size_t)n) {
            ok = false;
            break;
        }
        remaining -= (size_t)n;
    }
    if (fflush(file) != 0) ok = false;
    fclose(file);

    if (!ok || remaining != 0) {
        remove(temp);
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "upload interrupted");
    }

    if (remove(dest) != 0 && errno != ENOENT) {
        remove(temp);
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "cannot replace destination");
    }
    if (rename(temp, dest) != 0) {
        remove(temp);
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "cannot finalize upload");
    }
    storage_unlock();

    ESP_LOGI(TAG, "Uploaded %s:%s bytes=%u", volume, logical,
             (unsigned)req->content_len);
    return json_ok(req);
}

static esp_err_t parse_volume_path_form(httpd_req_t *req,
                                        char *body, size_t body_cap,
                                        char *volume, size_t volume_cap,
                                        char *path, size_t path_cap)
{
    esp_err_t e = read_form(req, body, body_cap);
    if (e != ESP_OK) return e;
    if (!form_value(body, "volume", volume, volume_cap) ||
        !form_value(body, "path", path, path_cap)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t mkdir_post(httpd_req_t *req)
{
    char body[640];
    char volume[16];
    char logical[LOGICAL_PATH_MAX + 1];
    char fs_path[256];
    if (parse_volume_path_form(req, body, sizeof(body),
                               volume, sizeof(volume), logical, sizeof(logical)) != ESP_OK ||
        resolve_path(volume, logical, fs_path, sizeof(fs_path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "invalid volume/path");
    }
    if (!storage_lock(3000)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "filesystem busy");
    }
    int rc = mkdir(fs_path, 0775);
    int saved_errno = errno;
    storage_unlock();
    if (rc != 0 && saved_errno != EEXIST) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mkdir failed");
    }
    return json_ok(req);
}

static esp_err_t delete_post(httpd_req_t *req)
{
    char body[640];
    char volume[16];
    char logical[LOGICAL_PATH_MAX + 1];
    char fs_path[256];
    if (parse_volume_path_form(req, body, sizeof(body),
                               volume, sizeof(volume), logical, sizeof(logical)) != ESP_OK ||
        !strcmp(logical, "/") ||
        resolve_path(volume, logical, fs_path, sizeof(fs_path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "invalid volume/path");
    }
    if (!storage_lock(3000)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "filesystem busy");
    }
    struct stat st = {0};
    int rc = -1;
    if (stat(fs_path, &st) == 0) {
        rc = S_ISDIR(st.st_mode) ? rmdir(fs_path) : remove(fs_path);
    }
    storage_unlock();
    if (rc != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "delete failed; directories must be empty");
    }
    return json_ok(req);
}

static esp_err_t rename_post(httpd_req_t *req)
{
    char body[800];
    char volume[16] = {0};
    char from[LOGICAL_PATH_MAX + 1] = {0};
    char to[LOGICAL_PATH_MAX + 1] = {0};
    char src[256];
    char dst[256];
    if (read_form(req, body, sizeof(body)) != ESP_OK ||
        !form_value(body, "volume", volume, sizeof(volume)) ||
        !form_value(body, "from", from, sizeof(from)) ||
        !form_value(body, "to", to, sizeof(to))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "volume/from/to required");
    }
    if (!strcmp(from, "/") ||
        resolve_path(volume, from, src, sizeof(src)) != ESP_OK ||
        resolve_path(volume, to, dst, sizeof(dst)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid path");
    }
    if (!storage_lock(3000)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "filesystem busy");
    }
    int rc = rename(src, dst);
    storage_unlock();
    if (rc != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "rename failed");
    }
    return json_ok(req);
}

static esp_err_t copy_post(httpd_req_t *req)
{
    char body[1000];
    char source_volume[16] = {0};
    char source_path[LOGICAL_PATH_MAX + 1] = {0};
    char dest_volume[16] = {0};
    char dest_path[LOGICAL_PATH_MAX + 1] = {0};
    char src[256];
    char dst[256];
    char temp[300];

    if (read_form(req, body, sizeof(body)) != ESP_OK ||
        !form_value(body, "source_volume", source_volume, sizeof(source_volume)) ||
        !form_value(body, "source_path", source_path, sizeof(source_path)) ||
        !form_value(body, "dest_volume", dest_volume, sizeof(dest_volume)) ||
        !form_value(body, "dest_path", dest_path, sizeof(dest_path))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "source/destination required");
    }
    if (resolve_path(source_volume, source_path, src, sizeof(src)) != ESP_OK ||
        resolve_path(dest_volume, dest_path, dst, sizeof(dst)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "invalid path or volume");
    }
    if (!strcmp(src, dst)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "source and destination are identical");
    }
    if (!storage_lock(5000)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "filesystem busy");
    }

    struct stat st = {0};
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) {
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "source must be a file");
    }

    storage_state_t storage = {0};
    storage_get_state(&storage);
    const storage_volume_state_t *dest_state = volume_state(&storage, dest_volume);
    if (!dest_state || !dest_state->mounted ||
        dest_state->free_bytes < (uint64_t)st.st_size) {
        storage_unlock();
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "not enough free space");
    }

    FILE *input = fopen(src, "rb");
    if (!input) {
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "source not found");
    }
    snprintf(temp, sizeof(temp), "%s.prisma-%08lx.part",
             dst, (unsigned long)esp_random());
    FILE *output = fopen(temp, "wb");
    if (!output) {
        fclose(input);
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "cannot create destination");
    }

    char buffer[IO_BUF_SIZE];
    size_t n;
    bool ok = true;
    while ((n = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1, n, output) != n) {
            ok = false;
            break;
        }
    }
    fclose(input);
    if (fflush(output) != 0) ok = false;
    fclose(output);

    if (!ok) {
        remove(temp);
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "copy failed");
    }
    if (remove(dst) != 0 && errno != ENOENT) {
        remove(temp);
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "cannot replace destination");
    }
    if (rename(temp, dst) != 0) {
        remove(temp);
        storage_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "copy finalize failed");
    }
    storage_unlock();
    return json_ok(req);
}

static esp_err_t internal_init_post(httpd_req_t *req)
{
    esp_err_t e = storage_internal_initialize();
    if (e != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   esp_err_to_name(e));
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"mounted\":true}");
}

static esp_err_t sd_retry_post(httpd_req_t *req)
{
    esp_err_t e = storage_sd_retry();
    if (e != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   esp_err_to_name(e));
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"mounted\":true}");
}

esp_err_t files_web_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri="/api/v1/storage", .method=HTTP_GET, .handler=storage_get},
        {.uri="/api/v1/storage/internal/init", .method=HTTP_POST, .handler=internal_init_post},
        {.uri="/api/v1/storage/sd/retry", .method=HTTP_POST, .handler=sd_retry_post},
        {.uri="/api/v1/files/list", .method=HTTP_GET, .handler=list_get},
        {.uri="/api/v1/files/download", .method=HTTP_GET, .handler=download_get},
        {.uri="/api/v1/files/upload", .method=HTTP_POST, .handler=upload_post},
        {.uri="/api/v1/files/mkdir", .method=HTTP_POST, .handler=mkdir_post},
        {.uri="/api/v1/files/delete", .method=HTTP_POST, .handler=delete_post},
        {.uri="/api/v1/files/rename", .method=HTTP_POST, .handler=rename_post},
        {.uri="/api/v1/files/copy", .method=HTTP_POST, .handler=copy_post}
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t e = httpd_register_uri_handler(server, &routes[i]);
        if (e != ESP_OK) return e;
    }
    ESP_LOGI(TAG, "Storage/files API ready: streamed dual-volume operations");
    return ESP_OK;
}
