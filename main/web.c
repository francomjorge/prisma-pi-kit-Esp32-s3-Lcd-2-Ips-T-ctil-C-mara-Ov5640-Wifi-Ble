#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "app.h"
#include "identity.h"

static const char *TAG = "CS005_WEB";
static httpd_handle_t s_server;

esp_err_t files_web_register(httpd_handle_t server);

extern const char home_start[] asm("_binary_home_html_start");
extern const char home_end[] asm("_binary_home_html_end");
extern const char admin_start[] asm("_binary_admin_html_start");
extern const char admin_end[] asm("_binary_admin_html_end");
extern const char camera_synth_start[] asm("_binary_camera_synth_html_start");
extern const char camera_synth_end[] asm("_binary_camera_synth_html_end");
extern const char shared_css_start[] asm("_binary_shared_css_start");
extern const char shared_css_end[] asm("_binary_shared_css_end");

static esp_err_t send_embedded(httpd_req_t *req, const char *start, const char *end, const char *type)
{
    size_t len = (size_t)(end - start);
    if (len && start[len - 1] == '\0') --len;
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, start, len);
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t home_get(httpd_req_t *r){return send_embedded(r,home_start,home_end,"text/html; charset=utf-8");}
static esp_err_t admin_get(httpd_req_t *r){return send_embedded(r,admin_start,admin_end,"text/html; charset=utf-8");}
static esp_err_t camera_synth_get(httpd_req_t *r){return send_embedded(r,camera_synth_start,camera_synth_end,"text/html; charset=utf-8");}
static esp_err_t camera_sync_alias_get(httpd_req_t *r){httpd_resp_set_status(r,"302 Found");httpd_resp_set_hdr(r,"Location","/camera-synth");return httpd_resp_send(r,NULL,0);}
static esp_err_t css_get(httpd_req_t *r){return send_embedded(r,shared_css_start,shared_css_end,"text/css; charset=utf-8");}

static void mac_text(uint8_t type, char out[18])
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, type) == ESP_OK) snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    else strlcpy(out, "unavailable", 18);
}

static void json_escape(const char *src, char *dst, size_t cap)
{
    size_t j = 0;
    if (!cap) return;
    for (size_t i = 0; src && src[i] && j + 2 < cap; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[j++]='\\'; dst[j++]=(char)c; }
        else if (c >= 32 && c < 127) dst[j++] = (char)c;
    }
    dst[j] = 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '+') { *w++ = ' '; ++r; }
        else if (*r == '%' && r[1] && r[2]) {
            int a = hexval(r[1]), b = hexval(r[2]);
            if (a >= 0 && b >= 0) { *w++ = (char)((a << 4) | b); r += 3; }
            else *w++ = *r++;
        } else *w++ = *r++;
    }
    *w = 0;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= cap) return ESP_ERR_INVALID_SIZE;
    int total = 0;
    while (total < req->content_len) {
        int n = httpd_req_recv(req, buf + total, req->content_len - total);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        total += n;
    }
    buf[total] = 0;
    return ESP_OK;
}

static bool form_value(const char *body, const char *key, char *out, size_t cap)
{
    if (httpd_query_key_value(body, key, out, cap) != ESP_OK) return false;
    url_decode(out);
    return true;
}

static bool parse_bool(const char *s, bool fallback)
{
    if (!s || !*s) return fallback;
    return !strcmp(s,"1") || !strcmp(s,"true") || !strcmp(s,"on") || !strcmp(s,"yes");
}

static const char *auth_name(uint8_t mode)
{
    switch (mode) {
        case 0: return "OPEN";
        case 1: return "WEP";
        case 2: return "WPA-PSK";
        case 3: return "WPA2-PSK";
        case 4: return "WPA/WPA2";
        case 5: return "WPA2-ENTERPRISE";
        case 6: return "WPA3-PSK";
        default: return "SECURED";
    }
}

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_wdt";
        case ESP_RST_TASK_WDT: return "task_wdt";
        case ESP_RST_WDT: return "other_wdt";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

static esp_err_t status_get(httpd_req_t *req)
{
    network_state_t net={0}; discovery_state_t d={0}; camera_hw_state_t cam={0}; touch_hw_state_t touch={0}; prisma_settings_t cfg={0}; storage_state_t storage={0};
    network_get_state(&net); discovery_get_state(&d); camera_hw_get_state(&cam); touch_hw_get_state(&touch); settings_get(&cfg); storage_get_state(&storage);
    esp_chip_info_t chip={0}; esp_chip_info(&chip);
    uint32_t flash_bytes=0; bool flash_ok=esp_flash_get_size(NULL,&flash_bytes)==ESP_OK;
    size_t psram_total=heap_caps_get_total_size(MALLOC_CAP_SPIRAM), psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t heap_free=(uint32_t)esp_get_free_heap_size(), heap_min=(uint32_t)esp_get_minimum_free_heap_size();
    int64_t uptime_s=esp_timer_get_time()/1000000;
    esp_reset_reason_t reset = esp_reset_reason();
    bool healthy=display_ready()&&net.ready&&cam.ready;
    char sta_mac[18], ap_mac[18]; mac_text(ESP_MAC_WIFI_STA,sta_mac); mac_text(ESP_MAC_WIFI_SOFTAP,ap_mac);
    const esp_partition_t *ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    char friend_e[80], host_e[80], ap_e[80], sta_e[80];
    json_escape(cfg.friendly_name,friend_e,sizeof(friend_e)); json_escape(cfg.hostname,host_e,sizeof(host_e));
    json_escape(net.ssid,ap_e,sizeof(ap_e)); json_escape(net.sta_ssid,sta_e,sizeof(sta_e));
    char int_err[120], sd_err[120]; json_escape(storage.internal.error,int_err,sizeof(int_err)); json_escape(storage.sd.error,sd_err,sizeof(sd_err));
    char json[7600];
    int n=snprintf(json,sizeof(json),
      "{\"schema\":\"prisma.status.v1\",\"product\":\"%s\",\"application\":\"%s\",\"vendor\":\"%s\",\"firmware\":\"%s\",\"build\":\"%s\",\"friendly_name\":\"%s\",\"hostname\":\"%s\",\"mdns_url\":\"%s\",\"settings_schema\":%u,\"uptime_s\":%" PRId64 ",\"reset_reason\":\"%s\",\"health\":\"%s\","
      "\"device\":{\"model\":\"ESP32-S3\",\"cores\":%d,\"revision\":%d,\"flash_bytes\":%" PRIu32 ",\"flash_read_ok\":%s,\"wifi_sta_mac\":\"%s\",\"wifi_ap_mac\":\"%s\"},"
      "\"memory\":{\"heap_free\":%" PRIu32 ",\"heap_min\":%" PRIu32 ",\"psram_total\":%u,\"psram_free\":%u},"
      "\"storage\":{\"shared_spi_ready\":%s,\"internal\":{\"mounted\":%s,\"fs\":\"%s\",\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu,\"max_open_files\":%u,\"error\":\"%s\"},\"sd\":{\"mounted\":%s,\"fs\":\"%s\",\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu,\"max_open_files\":%u,\"error\":\"%s\"},\"file_count_quota\":false,\"ota0\":%s,\"ota1\":%s},"
      "\"display\":{\"available\":true,\"enabled\":%s,\"state\":\"%s\",\"controller\":\"ST7789\",\"width\":240,\"height\":320,\"spi_hz\":40000000,\"pins\":{\"sclk\":39,\"mosi\":38,\"miso\":40,\"dc\":42,\"cs\":45,\"backlight\":1}},"
      "\"softap\":{\"available\":true,\"enabled\":%s,\"open\":%s,\"security\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"clients\":%u,\"channel\":%u,\"max_clients\":%u},"
      "\"sta\":{\"available\":true,\"configured\":%s,\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
      "\"mdns\":{\"available\":true,\"enabled\":%s,\"state\":\"%s\",\"hostname\":\"%s\",\"url\":\"%s\"},"
      "\"camera\":{\"available\":true,\"probed\":%s,\"enabled\":%s,\"state\":\"%s\",\"sensor\":\"%s\",\"expected_sensor\":%s,\"pid\":%u,\"pid_hex\":\"0x%04X\",\"midh\":%u,\"midl\":%u,\"ver\":%u,\"xclk_hz\":%u,\"fb_count\":%u,\"fb_location\":\"%s\",\"capture_enabled\":false,\"pins\":{\"pwdn\":17,\"xclk\":8,\"sccb_sda\":21,\"sccb_scl\":16,\"pclk\":9,\"vsync\":6,\"href\":4},\"error\":\"%s\"},"
      "\"touch\":{\"available\":true,\"probed\":%s,\"enabled\":false,\"state\":\"%s\",\"controller\":\"%s\",\"address\":%u,\"chip_id\":%u,\"chip_id_hex\":\"0x%02X\",\"sda\":48,\"scl\":47,\"gestures_enabled\":false,\"error\":\"%s\"},"
      "\"stream\":{\"available\":false,\"enabled\":false,\"state\":\"deferred_until_platform_ready\"},\"esp_now\":{\"available\":false,\"enabled\":false,\"state\":\"planned\"},\"ble\":{\"available\":false,\"enabled\":false,\"state\":\"planned\"}}",
      PRISMA_PRODUCT,PRISMA_APPLICATION,PRISMA_VENDOR,PRISMA_FIRMWARE_VERSION,PRISMA_BUILD_ID,friend_e,host_e,d.url,(unsigned)cfg.schema,uptime_s,reset_reason_name(reset),healthy?"ready":"degraded",
      chip.cores,chip.revision,flash_bytes,flash_ok?"true":"false",sta_mac,ap_mac,heap_free,heap_min,(unsigned)psram_total,(unsigned)psram_free,
      storage.shared_spi_ready?"true":"false",
      storage.internal.mounted?"true":"false",storage.internal.fs,(unsigned long long)storage.internal.total_bytes,(unsigned long long)storage.internal.used_bytes,(unsigned long long)storage.internal.free_bytes,storage.internal.max_open_files,int_err,
      storage.sd.mounted?"true":"false",storage.sd.fs,(unsigned long long)storage.sd.total_bytes,(unsigned long long)storage.sd.used_bytes,(unsigned long long)storage.sd.free_bytes,storage.sd.max_open_files,sd_err,ota0?"true":"false",ota1?"true":"false",
      display_ready()?"true":"false",display_ready()?"ready":"error",
      net.ready?"true":"false",net.open?"true":"false",net.open?"OPEN":"WPA2-PSK",ap_e,net.ip,net.clients,net.channel,net.max_clients,
      net.sta_configured?"true":"false",net.sta_connected?"true":"false",sta_e,net.sta_ip,net.sta_rssi,
      d.ready?"true":"false",d.ready?"ready":"error",host_e,d.url,
      cam.probed?"true":"false",cam.ready?"true":"false",cam.ready?"ready":"error",cam.sensor_name,cam.expected?"true":"false",cam.pid,cam.pid,cam.midh,cam.midl,cam.ver,(unsigned)cam.xclk_hz,(unsigned)cam.fb_count,cam.fb_location,cam.error,
      touch.probed?"true":"false",touch.ready?"ready":"error",touch.controller,touch.address,touch.chip_id,touch.chip_id,touch.error);
    if(n<0||n>=(int)sizeof(json))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"status overflow");
    return send_json(req,json);
}

static esp_err_t config_get(httpd_req_t *req)
{
    prisma_settings_t cfg={0}; settings_get(&cfg);
    char f[80],h[80],a[80],s[80]; json_escape(cfg.friendly_name,f,sizeof(f)); json_escape(cfg.hostname,h,sizeof(h)); json_escape(cfg.ap_ssid,a,sizeof(a)); json_escape(cfg.sta_ssid,s,sizeof(s));
    char json[1000];
    snprintf(json,sizeof(json),"{\"schema\":%u,\"friendly_name\":\"%s\",\"hostname\":\"%s\",\"ap\":{\"ssid\":\"%s\",\"open\":%s,\"password_configured\":%s,\"channel\":%u,\"max_clients\":%u},\"sta\":{\"enabled\":%s,\"ssid\":\"%s\",\"password_configured\":%s}}",
      (unsigned)cfg.schema,f,h,a,cfg.ap_open?"true":"false",cfg.ap_password[0]?"true":"false",cfg.ap_channel,cfg.ap_max_clients,cfg.sta_enabled?"true":"false",s,cfg.sta_password[0]?"true":"false");
    return send_json(req,json);
}

static esp_err_t config_network_post(httpd_req_t *req)
{
    char body[1024];
    if (read_body(req,body,sizeof(body)) != ESP_OK) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid body");
    prisma_settings_t old={0}; settings_get(&old);
    char friendly[33],host[33],ssid[33],pass[64],tmp[16];
    strlcpy(friendly,old.friendly_name,sizeof(friendly)); strlcpy(host,old.hostname,sizeof(host)); strlcpy(ssid,old.ap_ssid,sizeof(ssid)); pass[0]=0;
    form_value(body,"friendly_name",friendly,sizeof(friendly)); form_value(body,"hostname",host,sizeof(host)); form_value(body,"ap_ssid",ssid,sizeof(ssid));
    bool open=old.ap_open; if(form_value(body,"ap_open",tmp,sizeof(tmp))) open=parse_bool(tmp,old.ap_open);
    uint8_t channel=old.ap_channel; if(form_value(body,"channel",tmp,sizeof(tmp))) channel=(uint8_t)atoi(tmp);
    uint8_t max_clients=old.ap_max_clients; if(form_value(body,"max_clients",tmp,sizeof(tmp))) max_clients=(uint8_t)atoi(tmp);
    bool have_pass=form_value(body,"ap_password",pass,sizeof(pass));
    const char *effective_pass = (!open && (!have_pass || !pass[0])) ? old.ap_password : pass;
    esp_err_t e=settings_set_ap(ssid,open,effective_pass,channel,max_clients);
    if(e==ESP_OK)e=settings_set_identity(friendly,host);
    if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid network settings");
    return send_json(req,"{\"ok\":true,\"saved\":true,\"reboot_required\":true}");
}

static esp_err_t scan_post(httpd_req_t *req)
{
    wifi_ap_info_t aps[PRISMA_SCAN_MAX]={0}; size_t count=0;
    esp_err_t e=network_scan(aps,PRISMA_SCAN_MAX,&count);
    if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));
    char json[3600]; size_t off=0;
    off += snprintf(json+off,sizeof(json)-off,"{\"count\":%u,\"networks\":[",(unsigned)count);
    for(size_t i=0;i<count && off<sizeof(json)-120;++i){char ssid[100];json_escape(aps[i].ssid,ssid,sizeof(ssid));off+=snprintf(json+off,sizeof(json)-off,"%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%u,\"security\":\"%s\"}",i?",":"",ssid,aps[i].rssi,aps[i].channel,auth_name(aps[i].authmode));}
    snprintf(json+off,sizeof(json)-off,"]}");
    return send_json(req,json);
}

static esp_err_t sta_connect_post(httpd_req_t *req)
{
    char body[512],ssid[33]={0},pass[64]={0};
    if(read_body(req,body,sizeof(body))!=ESP_OK||!form_value(body,"ssid",ssid,sizeof(ssid)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ssid required");
    form_value(body,"password",pass,sizeof(pass));
    esp_err_t e=network_connect_sta(ssid,pass);
    if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,esp_err_to_name(e));
    return send_json(req,"{\"ok\":true,\"connecting\":true,\"fallback_ap\":true}");
}

static esp_err_t sta_forget_post(httpd_req_t *req)
{
    esp_err_t e=network_forget_sta();
    if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));
    return send_json(req,"{\"ok\":true,\"sta_configured\":false}");
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    esp_err_t e=send_json(req,"{\"ok\":true,\"rebooting\":true}");
    xTaskCreate(reboot_task,"reboot",2048,NULL,3,NULL);
    return e;
}

static esp_err_t capabilities_get(httpd_req_t *req)
{
    network_state_t net={0}; discovery_state_t d={0}; camera_hw_state_t cam={0}; touch_hw_state_t touch={0}; storage_state_t storage={0};
    network_get_state(&net); discovery_get_state(&d); camera_hw_get_state(&cam); touch_hw_get_state(&touch); storage_get_state(&storage);
    bool files = storage.internal.mounted || storage.sd.mounted;
    char json[3800];
    int n=snprintf(json,sizeof(json),
      "{\"schema\":\"prisma.capabilities.v1\",\"product\":\"%s\",\"capabilities\":["
      "{\"id\":\"settings\",\"available\":true,\"enabled\":true,\"state\":\"persistent_nvs\"},"
      "{\"id\":\"display\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"softap\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"wifi_sta\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"http\",\"available\":true,\"enabled\":true,\"state\":\"ready\"},"
      "{\"id\":\"mdns\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"internal_storage\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"sd_storage\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"file_browser\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"camera_hw\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"touch_hw\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"snapshot\",\"available\":false,\"enabled\":false,\"state\":\"deferred_until_platform_validated\"},"
      "{\"id\":\"stream\",\"available\":false,\"enabled\":false,\"state\":\"deferred\"},"
      "{\"id\":\"esp_now\",\"available\":false,\"enabled\":false,\"state\":\"planned\"},"
      "{\"id\":\"ble\",\"available\":false,\"enabled\":false,\"state\":\"planned\"},"
      "{\"id\":\"ota\",\"available\":false,\"enabled\":false,\"state\":\"planned\"}]}",
      PRISMA_PRODUCT,
      display_ready()?"true":"false",display_ready()?"ready":"error",
      net.ready?"true":"false",net.ready?"ready":"error",
      net.sta_configured?"true":"false",net.sta_connected?"connected":"available",
      d.ready?"true":"false",d.ready?"ready":"error",
      storage.internal.mounted?"true":"false",storage.internal.mounted?"mounted":"unavailable",
      storage.sd.mounted?"true":"false",storage.sd.mounted?"mounted":"unavailable",
      files?"true":"false",files?"ready":"no_mounted_volume",
      cam.ready?"true":"false",cam.ready?"ready":"error",
      touch.ready?"true":"false",touch.ready?"ready":"error");
    if(n<0||n>=(int)sizeof(json))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"capabilities overflow");
    return send_json(req,json);
}

esp_err_t web_start(void)
{
    if(s_server)return ESP_OK;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.stack_size=12288;
    cfg.max_uri_handlers=28;
    cfg.max_open_sockets=7;
    cfg.lru_purge_enable=true;
    esp_err_t e=httpd_start(&s_server,&cfg); if(e!=ESP_OK)return e;
    const httpd_uri_t routes[]={
      {.uri="/",.method=HTTP_GET,.handler=home_get},
      {.uri="/admin",.method=HTTP_GET,.handler=admin_get},
      {.uri="/camera-synth",.method=HTTP_GET,.handler=camera_synth_get},
      {.uri="/camera-sync",.method=HTTP_GET,.handler=camera_sync_alias_get},
      {.uri="/shared.css",.method=HTTP_GET,.handler=css_get},
      {.uri="/api/v1/status",.method=HTTP_GET,.handler=status_get},
      {.uri="/api/v1/capabilities",.method=HTTP_GET,.handler=capabilities_get},
      {.uri="/api/v1/config",.method=HTTP_GET,.handler=config_get},
      {.uri="/api/v1/config/network",.method=HTTP_POST,.handler=config_network_post},
      {.uri="/api/v1/network/scan",.method=HTTP_POST,.handler=scan_post},
      {.uri="/api/v1/network/connect",.method=HTTP_POST,.handler=sta_connect_post},
      {.uri="/api/v1/network/forget",.method=HTTP_POST,.handler=sta_forget_post},
      {.uri="/api/v1/system/reboot",.method=HTTP_POST,.handler=reboot_post}
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i){e=httpd_register_uri_handler(s_server,&routes[i]);if(e!=ESP_OK)return e;}
    e=files_web_register(s_server);if(e!=ESP_OK)return e;
    ESP_LOGI(TAG,"HTTP ready: admin foundation + dual-volume files service; camera capture still disabled");
    return ESP_OK;
}
