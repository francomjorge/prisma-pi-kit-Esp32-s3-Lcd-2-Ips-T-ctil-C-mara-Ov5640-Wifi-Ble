#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"
#include "identity.h"

static const char *TAG = "CS003_WEB";
static httpd_handle_t s_server;
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
static esp_err_t home_get(httpd_req_t *r){return send_embedded(r,home_start,home_end,"text/html; charset=utf-8");}
static esp_err_t admin_get(httpd_req_t *r){return send_embedded(r,admin_start,admin_end,"text/html; charset=utf-8");}
static esp_err_t camera_synth_get(httpd_req_t *r){return send_embedded(r,camera_synth_start,camera_synth_end,"text/html; charset=utf-8");}
static esp_err_t camera_sync_alias_get(httpd_req_t *r){httpd_resp_set_status(r,"302 Found");httpd_resp_set_hdr(r,"Location","/camera-synth");return httpd_resp_send(r,NULL,0);}
static esp_err_t css_get(httpd_req_t *r){return send_embedded(r,shared_css_start,shared_css_end,"text/css; charset=utf-8");}

static esp_err_t status_get(httpd_req_t *req)
{
    network_state_t net={0}; discovery_state_t d={0}; camera_hw_state_t cam={0}; touch_hw_state_t touch={0};
    network_get_state(&net); discovery_get_state(&d); camera_hw_get_state(&cam); touch_hw_get_state(&touch);
    esp_chip_info_t chip={0}; esp_chip_info(&chip);
    uint32_t flash_bytes=0; bool flash_ok=esp_flash_get_size(NULL,&flash_bytes)==ESP_OK;
    size_t psram_total=heap_caps_get_total_size(MALLOC_CAP_SPIRAM), psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t heap_free=(uint32_t)esp_get_free_heap_size(), heap_min=(uint32_t)esp_get_minimum_free_heap_size();
    int64_t uptime_s=esp_timer_get_time()/1000000;
    bool healthy=display_ready()&&net.ready&&cam.ready;
    char json[3600];
    int n=snprintf(json,sizeof(json),
      "{\"schema\":\"prisma.status.v1\",\"product\":\"%s\",\"application\":\"%s\",\"vendor\":\"%s\",\"firmware\":\"%s\",\"build\":\"%s\",\"hostname\":\"%s\",\"mdns_url\":\"%s\",\"uptime_s\":%" PRId64 ",\"health\":\"%s\","
      "\"device\":{\"model\":\"ESP32-S3\",\"cores\":%d,\"revision\":%d,\"flash_bytes\":%" PRIu32 ",\"flash_read_ok\":%s},"
      "\"memory\":{\"heap_free\":%" PRIu32 ",\"heap_min\":%" PRIu32 ",\"psram_total\":%u,\"psram_free\":%u},"
      "\"display\":{\"available\":true,\"enabled\":%s,\"state\":\"%s\",\"controller\":\"ST7789\",\"width\":240,\"height\":320},"
      "\"softap\":{\"available\":true,\"enabled\":%s,\"open\":true,\"ssid\":\"%s\",\"ip\":\"%s\",\"clients\":%u,\"max_clients\":4},"
      "\"mdns\":{\"available\":true,\"enabled\":%s,\"state\":\"%s\",\"hostname\":\"%s\",\"url\":\"%s\"},"
      "\"camera\":{\"available\":true,\"probed\":%s,\"enabled\":%s,\"state\":\"%s\",\"sensor\":\"%s\",\"expected_sensor\":%s,\"pid\":%u,\"pid_hex\":\"0x%04X\",\"midh\":%u,\"midl\":%u,\"ver\":%u,\"xclk_hz\":%u,\"fb_count\":%u,\"fb_location\":\"%s\",\"capture_enabled\":false,\"error\":\"%s\"},"
      "\"touch\":{\"available\":true,\"probed\":%s,\"enabled\":false,\"state\":\"%s\",\"controller\":\"%s\",\"address\":%u,\"chip_id\":%u,\"chip_id_hex\":\"0x%02X\",\"gestures_enabled\":false,\"error\":\"%s\"},"
      "\"stream\":{\"available\":false,\"enabled\":false,\"state\":\"planned\"},\"esp_now\":{\"available\":false,\"enabled\":false,\"state\":\"planned\"},\"ble\":{\"available\":false,\"enabled\":false,\"state\":\"planned\"}}",
      PRISMA_PRODUCT,PRISMA_APPLICATION,PRISMA_VENDOR,PRISMA_FIRMWARE_VERSION,PRISMA_BUILD_ID,PRISMA_HOSTNAME,PRISMA_MDNS_URL,uptime_s,healthy?"ready":"degraded",
      chip.cores,chip.revision,flash_bytes,flash_ok?"true":"false",heap_free,heap_min,(unsigned)psram_total,(unsigned)psram_free,
      display_ready()?"true":"false",display_ready()?"ready":"error",net.ready?"true":"false",net.ssid,net.ip,net.clients,d.ready?"true":"false",d.ready?"ready":"error",PRISMA_HOSTNAME,PRISMA_MDNS_URL,
      cam.probed?"true":"false",cam.ready?"true":"false",cam.ready?"ready":"error",cam.sensor_name,cam.expected?"true":"false",cam.pid,cam.pid,cam.midh,cam.midl,cam.ver,(unsigned)cam.xclk_hz,(unsigned)cam.fb_count,cam.fb_location,cam.error,
      touch.probed?"true":"false",touch.ready?"ready":"error",touch.controller,touch.address,touch.chip_id,touch.chip_id,touch.error);
    if(n<0||n>=(int)sizeof(json))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"status overflow");
    httpd_resp_set_type(req,"application/json"); httpd_resp_set_hdr(req,"Cache-Control","no-store"); return httpd_resp_send(req,json,n);
}

static esp_err_t capabilities_get(httpd_req_t *req)
{
    network_state_t net={0}; discovery_state_t d={0}; camera_hw_state_t cam={0}; touch_hw_state_t touch={0};
    network_get_state(&net); discovery_get_state(&d); camera_hw_get_state(&cam); touch_hw_get_state(&touch);
    char json[2800];
    int n=snprintf(json,sizeof(json),
      "{\"schema\":\"prisma.capabilities.v1\",\"product\":\"%s\",\"capabilities\":["
      "{\"id\":\"display\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"softap\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"http\",\"available\":true,\"enabled\":true,\"state\":\"ready\"},"
      "{\"id\":\"mdns\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"camera_hw\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"touch_hw\",\"available\":true,\"enabled\":%s,\"state\":\"%s\"},"
      "{\"id\":\"snapshot\",\"available\":false,\"enabled\":false,\"state\":\"planned_cs004\"},"
      "{\"id\":\"stream\",\"available\":false,\"enabled\":false,\"state\":\"planned_cs005\"},"
      "{\"id\":\"wifi_sta\",\"available\":false,\"enabled\":false,\"state\":\"planned\"},"
      "{\"id\":\"esp_now\",\"available\":false,\"enabled\":false,\"state\":\"planned\"},"
      "{\"id\":\"ble\",\"available\":false,\"enabled\":false,\"state\":\"planned\"}]}",
      PRISMA_PRODUCT,display_ready()?"true":"false",display_ready()?"ready":"error",net.ready?"true":"false",net.ready?"ready":"error",d.ready?"true":"false",d.ready?"ready":"error",cam.ready?"true":"false",cam.ready?"ready":"error",touch.ready?"true":"false",touch.ready?"ready":"error");
    if(n<0||n>=(int)sizeof(json))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"capabilities overflow");
    httpd_resp_set_type(req,"application/json"); httpd_resp_set_hdr(req,"Cache-Control","no-store"); return httpd_resp_send(req,json,n);
}

esp_err_t web_start(void)
{
    if(s_server)return ESP_OK;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.stack_size=8192; cfg.max_uri_handlers=9; cfg.max_open_sockets=7; cfg.lru_purge_enable=true;
    esp_err_t e=httpd_start(&s_server,&cfg); if(e!=ESP_OK)return e;
    const httpd_uri_t routes[]={{.uri="/",.method=HTTP_GET,.handler=home_get},{.uri="/admin",.method=HTTP_GET,.handler=admin_get},{.uri="/camera-synth",.method=HTTP_GET,.handler=camera_synth_get},{.uri="/camera-sync",.method=HTTP_GET,.handler=camera_sync_alias_get},{.uri="/shared.css",.method=HTTP_GET,.handler=css_get},{.uri="/api/v1/status",.method=HTTP_GET,.handler=status_get},{.uri="/api/v1/capabilities",.method=HTTP_GET,.handler=capabilities_get}};
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i){e=httpd_register_uri_handler(s_server,&routes[i]);if(e!=ESP_OK)return e;}
    ESP_LOGI(TAG,"HTTP ready: CS003 hardware inventory /admin /camera-synth API v1");
    return ESP_OK;
}
