#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "app.h"
#include "identity.h"

static void json_escape(const char *src, char *dst, size_t cap)
{
    size_t j = 0;
    if (!cap) return;
    for (size_t i = 0; src && src[i] && j + 2 < cap; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[j++]='\\'; dst[j++]=(char)c; }
        else if (c == '\n') { dst[j++]='\\'; dst[j++]='n'; }
        else if (c == '\r') { dst[j++]='\\'; dst[j++]='r'; }
        else if (c == '\t') { dst[j++]='\\'; dst[j++]='t'; }
        else if (c >= 32) dst[j++] = (char)c;
    }
    dst[j] = 0;
}

static bool query_bool(httpd_req_t *req, const char *key)
{
    size_t len = httpd_req_get_url_query_len(req);
    if (!len || len > 127) return false;
    char query[128], value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) return false;
    return !strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "yes");
}

static esp_err_t read_json_body(httpd_req_t *req, char **out)
{
    if (!out || req->content_len == 0 || req->content_len > 8192) return ESP_ERR_INVALID_SIZE;
    char *body = malloc(req->content_len + 1);
    if (!body) return ESP_ERR_NO_MEM;
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { free(body); return ESP_FAIL; }
        got += (size_t)n;
    }
    body[got] = 0;
    *out = body;
    return ESP_OK;
}

static esp_err_t health_get(httpd_req_t *req)
{
    network_state_t net={0}; storage_state_t storage={0}; camera_hw_state_t cam={0};
    touch_hw_state_t touch={0}; imu_hw_state_t imu={0}; discovery_state_t discovery={0};
    network_get_state(&net); storage_get_state(&storage); camera_hw_get_state(&cam);
    touch_hw_get_state(&touch); imu_hw_get_state(&imu); discovery_get_state(&discovery);
    uint32_t heap_free=(uint32_t)esp_get_free_heap_size();
    uint32_t heap_min=(uint32_t)esp_get_minimum_free_heap_size();
    size_t psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    bool core_ok=display_ready()&&net.ready&&cam.ready;
    bool infra_ok=storage.internal.mounted||storage.sd.mounted;
    char json[2600];
    int n=snprintf(json,sizeof(json),
      "{\"schema\":\"prisma.health.v1\",\"status\":\"%s\",\"uptime_ms\":%llu,\"event_count\":%u,"
      "\"checks\":{"
      "\"display\":\"%s\",\"network_ap\":\"%s\",\"network_sta\":\"%s\",\"mdns\":\"%s\","
      "\"internal_storage\":\"%s\",\"sd_storage\":\"%s\",\"camera_hw\":\"%s\",\"touch_hw\":\"%s\",\"imu_hw\":\"%s\"},"
      "\"memory\":{\"heap_free\":%u,\"heap_min\":%u,\"psram_free\":%u},"
      "\"policy\":{\"camera_capture\":false,\"stream\":false,\"touch_navigation\":false,\"admin_auth\":false}}",
      core_ok&&infra_ok?"ready":core_ok?"degraded":"error",
      (unsigned long long)(esp_timer_get_time()/1000),platform_log_total_events(),
      display_ready()?"ready":"error",net.ready?"ready":"error",
      net.sta_connected?"connected":net.sta_configured?"configured":"off",
      discovery.ready?"ready":"error",
      storage.internal.mounted?"mounted":"unavailable",
      storage.sd.mounted?"mounted":"unavailable",
      cam.ready?"ready":"error",touch.ready?"ready":"error",imu.ready?"ready":"error",
      heap_free,heap_min,(unsigned)psram_free);
    if(n<0||n>=(int)sizeof(json))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"health overflow");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,json,n);
}

static esp_err_t logs_get(httpd_req_t *req)
{
    platform_event_t events[PRISMA_EVENT_LOG_CAPACITY];
    size_t count=platform_log_copy(events,PRISMA_EVENT_LOG_CAPACITY);
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char head[128];snprintf(head,sizeof(head),"{\"total\":%u,\"retained\":%u,\"events\":[",platform_log_total_events(),(unsigned)count);
    esp_err_t e=httpd_resp_send_chunk(req,head,HTTPD_RESP_USE_STRLEN);
    for(size_t i=0;i<count&&e==ESP_OK;i++){
        char l[24],s[48],m[260],item[480];json_escape(events[i].level,l,sizeof(l));json_escape(events[i].subsystem,s,sizeof(s));json_escape(events[i].message,m,sizeof(m));
        int n=snprintf(item,sizeof(item),"%s{\"sequence\":%u,\"uptime_ms\":%llu,\"level\":\"%s\",\"subsystem\":\"%s\",\"message\":\"%s\"}",i?",":"",events[i].sequence,(unsigned long long)events[i].uptime_ms,l,s,m);
        if(n>0&&n<(int)sizeof(item))e=httpd_resp_send_chunk(req,item,n);
    }
    if(e==ESP_OK)e=httpd_resp_send_chunk(req,"]}",2);
    if(e==ESP_OK)e=httpd_resp_send_chunk(req,NULL,0);
    return e;
}

static esp_err_t config_export_get(httpd_req_t *req)
{
    prisma_settings_t cfg={0};settings_get(&cfg);bool secrets=query_bool(req,"include_secrets");
    cJSON *root=cJSON_CreateObject();if(!root)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"no memory");
    cJSON_AddStringToObject(root,"schema","prisma.config.v1");cJSON_AddNumberToObject(root,"settings_schema",cfg.schema);cJSON_AddStringToObject(root,"product",PRISMA_PRODUCT);
    cJSON_AddStringToObject(root,"friendly_name",cfg.friendly_name);cJSON_AddStringToObject(root,"hostname",cfg.hostname);
    cJSON *ap=cJSON_AddObjectToObject(root,"ap");cJSON_AddStringToObject(ap,"ssid",cfg.ap_ssid);cJSON_AddBoolToObject(ap,"open",cfg.ap_open);cJSON_AddNumberToObject(ap,"channel",cfg.ap_channel);cJSON_AddNumberToObject(ap,"max_clients",cfg.ap_max_clients);cJSON_AddStringToObject(ap,"password",secrets?cfg.ap_password:"");cJSON_AddBoolToObject(ap,"password_redacted",!secrets&&cfg.ap_password[0]);
    cJSON *sta=cJSON_AddObjectToObject(root,"sta");cJSON_AddBoolToObject(sta,"enabled",cfg.sta_enabled);cJSON_AddStringToObject(sta,"ssid",cfg.sta_ssid);cJSON_AddStringToObject(sta,"password",secrets?cfg.sta_password:"");cJSON_AddBoolToObject(sta,"password_redacted",!secrets&&cfg.sta_password[0]);
    cJSON_AddBoolToObject(root,"includes_secrets",secrets);
    char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!json)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"no memory");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Content-Disposition","attachment; filename=\"prisma-camera-synth-config.json\"");
    esp_err_t e=httpd_resp_sendstr(req,json);free(json);return e;
}

static bool json_string(cJSON *obj,const char *key,char *out,size_t cap,bool required)
{
    cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);if(!v){return !required;}if(!cJSON_IsString(v)||!v->valuestring)return false;strlcpy(out,v->valuestring,cap);return true;
}

static esp_err_t config_import_post(httpd_req_t *req)
{
    char *body=NULL;esp_err_t e=read_json_body(req,&body);if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid JSON body");
    cJSON *root=cJSON_Parse(body);free(body);if(!root)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid JSON");
    prisma_settings_t next={0};settings_get(&next);next.schema=PRISMA_SETTINGS_SCHEMA;
    bool ok=json_string(root,"friendly_name",next.friendly_name,sizeof(next.friendly_name),true)&&json_string(root,"hostname",next.hostname,sizeof(next.hostname),true);
    cJSON *ap=cJSON_GetObjectItemCaseSensitive(root,"ap"),*sta=cJSON_GetObjectItemCaseSensitive(root,"sta");
    if(!cJSON_IsObject(ap)||!cJSON_IsObject(sta))ok=false;
    if(ok){ok=json_string(ap,"ssid",next.ap_ssid,sizeof(next.ap_ssid),true);cJSON *v=cJSON_GetObjectItemCaseSensitive(ap,"open");if(!cJSON_IsBool(v))ok=false;else next.ap_open=cJSON_IsTrue(v);v=cJSON_GetObjectItemCaseSensitive(ap,"channel");if(!cJSON_IsNumber(v))ok=false;else next.ap_channel=(uint8_t)v->valueint;v=cJSON_GetObjectItemCaseSensitive(ap,"max_clients");if(!cJSON_IsNumber(v))ok=false;else next.ap_max_clients=(uint8_t)v->valueint;char pass[64]={0};if(json_string(ap,"password",pass,sizeof(pass),false)&&pass[0])strlcpy(next.ap_password,pass,sizeof(next.ap_password));if(next.ap_open)next.ap_password[0]=0;}
    if(ok){cJSON *v=cJSON_GetObjectItemCaseSensitive(sta,"enabled");if(!cJSON_IsBool(v))ok=false;else next.sta_enabled=cJSON_IsTrue(v);ok=ok&&json_string(sta,"ssid",next.sta_ssid,sizeof(next.sta_ssid),!next.sta_enabled?false:true);char pass[64]={0};if(json_string(sta,"password",pass,sizeof(pass),false)&&pass[0])strlcpy(next.sta_password,pass,sizeof(next.sta_password));if(!next.sta_enabled){next.sta_ssid[0]=0;next.sta_password[0]=0;}}
    cJSON_Delete(root);if(!ok)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"configuration fields invalid");
    e=settings_apply(&next);if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"configuration rejected");
    platform_log_event("INFO","config","Configuration imported; reboot required");
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true,\"saved\":true,\"reboot_required\":true}");
}

static esp_err_t factory_reset_post(httpd_req_t *req)
{
    esp_err_t e=settings_factory_reset();if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));
    platform_log_event("WARN","config","Settings reset to factory defaults; files preserved");
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true,\"scope\":\"settings_only\",\"files_preserved\":true,\"reboot_required\":true}");
}

esp_err_t platform_web_register(void *server_handle)
{
    httpd_handle_t server=(httpd_handle_t)server_handle;
    const httpd_uri_t routes[]={
        {.uri="/api/v1/health",.method=HTTP_GET,.handler=health_get},
        {.uri="/api/v1/logs",.method=HTTP_GET,.handler=logs_get},
        {.uri="/api/v1/config/export",.method=HTTP_GET,.handler=config_export_get},
        {.uri="/api/v1/config/import",.method=HTTP_POST,.handler=config_import_post},
        {.uri="/api/v1/config/factory-reset",.method=HTTP_POST,.handler=factory_reset_post}
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i){esp_err_t e=httpd_register_uri_handler(server,&routes[i]);if(e!=ESP_OK)return e;}
    return ESP_OK;
}
