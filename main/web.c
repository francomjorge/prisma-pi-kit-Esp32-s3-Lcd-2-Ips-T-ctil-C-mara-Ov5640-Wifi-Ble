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

static const char *TAG="CS006_WEB";
static httpd_handle_t s_server;
esp_err_t files_web_register(httpd_handle_t server);

extern const char home_start[] asm("_binary_home_html_start");extern const char home_end[] asm("_binary_home_html_end");
extern const char admin_start[] asm("_binary_admin_html_start");extern const char admin_end[] asm("_binary_admin_html_end");
extern const char admin_css_start[] asm("_binary_admin_css_start");extern const char admin_css_end[] asm("_binary_admin_css_end");
extern const char admin_js_start[] asm("_binary_admin_js_start");extern const char admin_js_end[] asm("_binary_admin_js_end");
extern const char camera_synth_start[] asm("_binary_camera_synth_html_start");extern const char camera_synth_end[] asm("_binary_camera_synth_html_end");
extern const char shared_css_start[] asm("_binary_shared_css_start");extern const char shared_css_end[] asm("_binary_shared_css_end");

static esp_err_t send_embedded(httpd_req_t *r,const char *a,const char *b,const char *type){size_t n=(size_t)(b-a);if(n&&a[n-1]=='\0')--n;httpd_resp_set_type(r,type);httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_send(r,a,n);}
static esp_err_t send_json(httpd_req_t *r,const char *s){httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_sendstr(r,s);}
static esp_err_t home_get(httpd_req_t*r){return send_embedded(r,home_start,home_end,"text/html; charset=utf-8");}
static esp_err_t admin_get(httpd_req_t*r){return send_embedded(r,admin_start,admin_end,"text/html; charset=utf-8");}
static esp_err_t admin_css_get(httpd_req_t*r){return send_embedded(r,admin_css_start,admin_css_end,"text/css; charset=utf-8");}
static esp_err_t admin_js_get(httpd_req_t*r){return send_embedded(r,admin_js_start,admin_js_end,"application/javascript; charset=utf-8");}
static esp_err_t synth_get(httpd_req_t*r){return send_embedded(r,camera_synth_start,camera_synth_end,"text/html; charset=utf-8");}
static esp_err_t alias_get(httpd_req_t*r){httpd_resp_set_status(r,"302 Found");httpd_resp_set_hdr(r,"Location","/camera-synth");return httpd_resp_send(r,NULL,0);}
static esp_err_t css_get(httpd_req_t*r){return send_embedded(r,shared_css_start,shared_css_end,"text/css; charset=utf-8");}

static void mac_text(uint8_t type,char out[18]){uint8_t m[6]={0};if(esp_read_mac(m,type)==ESP_OK)snprintf(out,18,"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]);else strlcpy(out,"unavailable",18);}
static void esc_json(const char*s,char*d,size_t cap){size_t j=0;if(!cap)return;for(size_t i=0;s&&s[i]&&j+2<cap;i++){unsigned char c=s[i];if(c=='"'||c=='\\'){d[j++]='\\';d[j++]=(char)c;}else if(c>=32&&c<127)d[j++]=(char)c;}d[j]=0;}
static int hx(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return-1;}
static void url_decode(char*s){char*r=s,*w=s;while(*r){if(*r=='+'){*w++=' ';r++;}else if(*r=='%'&&r[1]&&r[2]){int a=hx(r[1]),b=hx(r[2]);if(a>=0&&b>=0){*w++=(char)((a<<4)|b);r+=3;}else*w++=*r++;}else*w++=*r++;}*w=0;}
static esp_err_t read_body(httpd_req_t*r,char*b,size_t cap){if(r->content_len==0||r->content_len>=cap)return ESP_ERR_INVALID_SIZE;size_t got=0;while(got<r->content_len){int n=httpd_req_recv(r,b+got,r->content_len-got);if(n==HTTPD_SOCK_ERR_TIMEOUT)continue;if(n<=0)return ESP_FAIL;got+=(size_t)n;}b[got]=0;return ESP_OK;}
static bool form_value(const char*body,const char*k,char*out,size_t cap){if(httpd_query_key_value(body,k,out,cap)!=ESP_OK)return false;url_decode(out);return true;}
static bool parse_bool(const char*s,bool f){if(!s||!*s)return f;return!strcmp(s,"1")||!strcmp(s,"true")||!strcmp(s,"on")||!strcmp(s,"yes");}
static const char*auth_name(uint8_t m){switch(m){case 0:return"OPEN";case 2:return"WPA-PSK";case 3:return"WPA2-PSK";case 4:return"WPA/WPA2";case 6:return"WPA3-PSK";default:return"SECURED";}}
static const char*reset_name(esp_reset_reason_t r){switch(r){case ESP_RST_POWERON:return"power_on";case ESP_RST_SW:return"software";case ESP_RST_PANIC:return"panic";case ESP_RST_INT_WDT:return"interrupt_wdt";case ESP_RST_TASK_WDT:return"task_wdt";case ESP_RST_WDT:return"other_wdt";case ESP_RST_DEEPSLEEP:return"deep_sleep";case ESP_RST_BROWNOUT:return"brownout";default:return"unknown";}}

static esp_err_t status_get(httpd_req_t*r){
 network_state_t net={0};discovery_state_t d={0};camera_hw_state_t cam={0};touch_hw_state_t touch={0};imu_hw_state_t imu={0};prisma_settings_t cfg={0};storage_state_t st={0};
 network_get_state(&net);discovery_get_state(&d);camera_hw_get_state(&cam);touch_hw_get_state(&touch);imu_hw_get_state(&imu);settings_get(&cfg);storage_get_state(&st);
 esp_chip_info_t chip={0};esp_chip_info(&chip);uint32_t flash=0;bool flash_ok=esp_flash_get_size(NULL,&flash)==ESP_OK;size_t ps_total=heap_caps_get_total_size(MALLOC_CAP_SPIRAM),ps_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);uint32_t heap=(uint32_t)esp_get_free_heap_size(),heap_min=(uint32_t)esp_get_minimum_free_heap_size();
 char sta_mac[18],ap_mac[18],fn[80],host[80],ap[80],sta[80],ie[120],se[120],imu_e[100];mac_text(ESP_MAC_WIFI_STA,sta_mac);mac_text(ESP_MAC_WIFI_SOFTAP,ap_mac);esc_json(cfg.friendly_name,fn,sizeof(fn));esc_json(cfg.hostname,host,sizeof(host));esc_json(net.ssid,ap,sizeof(ap));esc_json(net.sta_ssid,sta,sizeof(sta));esc_json(st.internal.error,ie,sizeof(ie));esc_json(st.sd.error,se,sizeof(se));esc_json(imu.error,imu_e,sizeof(imu_e));
 const esp_partition_t*ota0=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,NULL);const esp_partition_t*ota1=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,NULL);bool healthy=display_ready()&&net.ready&&cam.ready;
 char json[8500];int n=snprintf(json,sizeof(json),
 "{\"schema\":\"prisma.status.v1\",\"product\":\"%s\",\"application\":\"%s\",\"vendor\":\"%s\",\"firmware\":\"%s\",\"build\":\"%s\",\"friendly_name\":\"%s\",\"hostname\":\"%s\",\"mdns_url\":\"%s\",\"settings_schema\":%u,\"uptime_s\":%llu,\"reset_reason\":\"%s\",\"health\":\"%s\","
 "\"device\":{\"model\":\"ESP32-S3\",\"cores\":%d,\"revision\":%d,\"flash_bytes\":%u,\"flash_read_ok\":%s,\"wifi_sta_mac\":\"%s\",\"wifi_ap_mac\":\"%s\"},"
 "\"memory\":{\"heap_free\":%u,\"heap_min\":%u,\"psram_total\":%u,\"psram_free\":%u},"
 "\"i2c\":{\"state\":\"%s\",\"port\":0,\"sda\":48,\"scl\":47,\"clock_hz\":400000},"
 "\"storage\":{\"shared_spi_ready\":%s,\"internal\":{\"mounted\":%s,\"fs\":\"%s\",\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu,\"max_open_files\":%u,\"error\":\"%s\"},\"sd\":{\"mounted\":%s,\"fs\":\"%s\",\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu,\"max_open_files\":%u,\"error\":\"%s\"},\"file_count_quota\":false,\"ota0\":%s,\"ota1\":%s},"
 "\"display\":{\"available\":true,\"enabled\":%s,\"state\":\"%s\",\"controller\":\"ST7789T3\",\"width\":240,\"height\":320},"
 "\"softap\":{\"available\":true,\"enabled\":%s,\"open\":%s,\"security\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"clients\":%u,\"channel\":%u,\"max_clients\":%u},"
 "\"sta\":{\"available\":true,\"configured\":%s,\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
 "\"mdns\":{\"state\":\"%s\",\"url\":\"%s\"},"
 "\"touch\":{\"state\":\"%s\",\"controller\":\"%s\",\"address\":%u,\"chip_id_hex\":\"0x%02X\",\"error\":\"%s\"},"
 "\"imu\":{\"state\":\"%s\",\"controller\":\"%s\",\"address\":%u,\"address_hex\":\"0x%02X\",\"who_am_i\":%u,\"who_am_i_hex\":\"0x%02X\",\"revision\":%u,\"revision_hex\":\"0x%02X\",\"sampling_enabled\":false,\"error\":\"%s\"},"
 "\"camera\":{\"state\":\"%s\",\"sensor\":\"%s\",\"expected_sensor\":%s,\"pid_hex\":\"0x%04X\",\"xclk_hz\":%u,\"fb_count\":%u,\"fb_location\":\"%s\",\"capture_enabled\":false,\"error\":\"%s\"},"
 "\"stream\":{\"state\":\"deferred\"},\"esp_now\":{\"state\":\"planned\"},\"ble\":{\"state\":\"planned\"}}",
 PRISMA_PRODUCT,PRISMA_APPLICATION,PRISMA_VENDOR,PRISMA_FIRMWARE_VERSION,PRISMA_BUILD_ID,fn,host,d.url,(unsigned)cfg.schema,(unsigned long long)(esp_timer_get_time()/1000000),reset_name(esp_reset_reason()),healthy?"ready":"degraded",
 chip.cores,chip.revision,(unsigned)flash,flash_ok?"true":"false",sta_mac,ap_mac,(unsigned)heap,(unsigned)heap_min,(unsigned)ps_total,(unsigned)ps_free,board_i2c_ready()?"ready":"error",st.shared_spi_ready?"true":"false",
 st.internal.mounted?"true":"false",st.internal.fs,(unsigned long long)st.internal.total_bytes,(unsigned long long)st.internal.used_bytes,(unsigned long long)st.internal.free_bytes,st.internal.max_open_files,ie,
 st.sd.mounted?"true":"false",st.sd.fs,(unsigned long long)st.sd.total_bytes,(unsigned long long)st.sd.used_bytes,(unsigned long long)st.sd.free_bytes,st.sd.max_open_files,se,ota0?"true":"false",ota1?"true":"false",
 display_ready()?"true":"false",display_ready()?"ready":"error",net.ready?"true":"false",net.open?"true":"false",net.open?"OPEN":"WPA2-PSK",ap,net.ip,net.clients,net.channel,net.max_clients,
 net.sta_configured?"true":"false",net.sta_connected?"true":"false",sta,net.sta_ip,net.sta_rssi,d.ready?"ready":"error",d.url,
 touch.ready?"ready":"error",touch.controller,touch.address,touch.chip_id,touch.error,
 imu.ready?"ready":"error",imu.controller,imu.address,imu.address,imu.who_am_i,imu.who_am_i,imu.revision,imu.revision,imu_e,
 cam.ready?"ready":"error",cam.sensor_name,cam.expected?"true":"false",cam.pid,(unsigned)cam.xclk_hz,(unsigned)cam.fb_count,cam.fb_location,cam.error);
 if(n<0||n>=(int)sizeof(json)){
  return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"status overflow");
 }
 return httpd_resp_send(r,json,n);
}

static esp_err_t config_get(httpd_req_t*r){prisma_settings_t c={0};settings_get(&c);char f[80],h[80],a[80],s[80];esc_json(c.friendly_name,f,sizeof(f));esc_json(c.hostname,h,sizeof(h));esc_json(c.ap_ssid,a,sizeof(a));esc_json(c.sta_ssid,s,sizeof(s));char j[1100];snprintf(j,sizeof(j),"{\"schema\":%u,\"friendly_name\":\"%s\",\"hostname\":\"%s\",\"ap\":{\"ssid\":\"%s\",\"open\":%s,\"password_configured\":%s,\"channel\":%u,\"max_clients\":%u},\"sta\":{\"enabled\":%s,\"ssid\":\"%s\",\"password_configured\":%s}}",(unsigned)c.schema,f,h,a,c.ap_open?"true":"false",c.ap_password[0]?"true":"false",c.ap_channel,c.ap_max_clients,c.sta_enabled?"true":"false",s,c.sta_password[0]?"true":"false");return send_json(r,j);}
static esp_err_t config_network_post(httpd_req_t*r){char b[1024];if(read_body(r,b,sizeof(b))!=ESP_OK)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"invalid body");prisma_settings_t old={0};settings_get(&old);char friendly[33],host[33],ssid[33],pass[64]={0},tmp[16];strlcpy(friendly,old.friendly_name,sizeof(friendly));strlcpy(host,old.hostname,sizeof(host));strlcpy(ssid,old.ap_ssid,sizeof(ssid));form_value(b,"friendly_name",friendly,sizeof(friendly));form_value(b,"hostname",host,sizeof(host));form_value(b,"ap_ssid",ssid,sizeof(ssid));bool open=old.ap_open;if(form_value(b,"ap_open",tmp,sizeof(tmp)))open=parse_bool(tmp,old.ap_open);uint8_t channel=old.ap_channel;if(form_value(b,"channel",tmp,sizeof(tmp)))channel=(uint8_t)atoi(tmp);uint8_t max=old.ap_max_clients;if(form_value(b,"max_clients",tmp,sizeof(tmp)))max=(uint8_t)atoi(tmp);bool hp=form_value(b,"ap_password",pass,sizeof(pass));const char*ep=(!open&&(!hp||!pass[0]))?old.ap_password:pass;esp_err_t e=settings_set_ap(ssid,open,ep,channel,max);if(e==ESP_OK)e=settings_set_identity(friendly,host);if(e!=ESP_OK)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"invalid network settings");platform_log_event("INFO","network","AP/identity settings saved; reboot required");return send_json(r,"{\"ok\":true,\"saved\":true,\"reboot_required\":true}");}
static esp_err_t scan_post(httpd_req_t*r){wifi_ap_info_t aps[PRISMA_SCAN_MAX]={0};size_t count=0;esp_err_t e=network_scan(aps,PRISMA_SCAN_MAX,&count);if(e!=ESP_OK)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));char j[3600];size_t o=(size_t)snprintf(j,sizeof(j),"{\"count\":%u,\"networks\":[",(unsigned)count);for(size_t i=0;i<count&&o<sizeof(j)-120;i++){char s[100];esc_json(aps[i].ssid,s,sizeof(s));o+=(size_t)snprintf(j+o,sizeof(j)-o,"%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%u,\"security\":\"%s\"}",i?",":"",s,aps[i].rssi,aps[i].channel,auth_name(aps[i].authmode));}snprintf(j+o,sizeof(j)-o,"]}");return send_json(r,j);}
static esp_err_t sta_connect_post(httpd_req_t*r){char b[512],ssid[33]={0},pass[64]={0};if(read_body(r,b,sizeof(b))!=ESP_OK||!form_value(b,"ssid",ssid,sizeof(ssid)))return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"ssid required");form_value(b,"password",pass,sizeof(pass));esp_err_t e=network_connect_sta(ssid,pass);if(e!=ESP_OK)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,esp_err_to_name(e));platform_log_event("INFO","network","STA connection requested");return send_json(r,"{\"ok\":true,\"connecting\":true,\"fallback_ap\":true}");}
static esp_err_t sta_forget_post(httpd_req_t*r){esp_err_t e=network_forget_sta();if(e!=ESP_OK)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));platform_log_event("INFO","network","STA credentials forgotten");return send_json(r,"{\"ok\":true}");}
static void reboot_task(void*a){vTaskDelay(pdMS_TO_TICKS(700));esp_restart();}
static esp_err_t reboot_post(httpd_req_t*r){platform_log_event("WARN","system","Reboot requested from Admin");esp_err_t e=send_json(r,"{\"ok\":true,\"rebooting\":true}");xTaskCreate(reboot_task,"reboot",2048,NULL,3,NULL);return e;}
static esp_err_t capabilities_get(httpd_req_t*r){storage_state_t st={0};imu_hw_state_t imu={0};storage_get_state(&st);imu_hw_get_state(&imu);char j[2100];snprintf(j,sizeof(j),"{\"schema\":\"prisma.capabilities.v1\",\"product\":\"%s\",\"capabilities\":[{\"id\":\"admin\",\"state\":\"ready\"},{\"id\":\"settings\",\"state\":\"persistent_nvs\"},{\"id\":\"files\",\"state\":\"%s\"},{\"id\":\"internal_storage\",\"state\":\"%s\"},{\"id\":\"sd_storage\",\"state\":\"%s\"},{\"id\":\"imu_hw\",\"state\":\"%s\"},{\"id\":\"health\",\"state\":\"ready\"},{\"id\":\"event_log\",\"state\":\"ready\"},{\"id\":\"config_backup\",\"state\":\"ready\"},{\"id\":\"snapshot\",\"state\":\"deferred\"},{\"id\":\"stream\",\"state\":\"deferred\"},{\"id\":\"esp_now\",\"state\":\"planned\"},{\"id\":\"ble\",\"state\":\"planned\"},{\"id\":\"admin_auth\",\"state\":\"planned\"},{\"id\":\"ota\",\"state\":\"planned\"}]}",PRISMA_PRODUCT,(st.internal.mounted||st.sd.mounted)?"ready":"no_volume",st.internal.mounted?"mounted":"unavailable",st.sd.mounted?"mounted":"unavailable",imu.ready?"ready":"error");return send_json(r,j);}

esp_err_t web_start(void){if(s_server)return ESP_OK;httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.stack_size=12288;c.max_uri_handlers=36;c.max_open_sockets=7;c.lru_purge_enable=true;esp_err_t e=httpd_start(&s_server,&c);if(e!=ESP_OK)return e;const httpd_uri_t routes[]={
 {.uri="/",.method=HTTP_GET,.handler=home_get},{.uri="/admin",.method=HTTP_GET,.handler=admin_get},{.uri="/admin.css",.method=HTTP_GET,.handler=admin_css_get},{.uri="/admin.js",.method=HTTP_GET,.handler=admin_js_get},{.uri="/camera-synth",.method=HTTP_GET,.handler=synth_get},{.uri="/camera-sync",.method=HTTP_GET,.handler=alias_get},{.uri="/shared.css",.method=HTTP_GET,.handler=css_get},{.uri="/api/v1/status",.method=HTTP_GET,.handler=status_get},{.uri="/api/v1/capabilities",.method=HTTP_GET,.handler=capabilities_get},{.uri="/api/v1/config",.method=HTTP_GET,.handler=config_get},{.uri="/api/v1/config/network",.method=HTTP_POST,.handler=config_network_post},{.uri="/api/v1/network/scan",.method=HTTP_POST,.handler=scan_post},{.uri="/api/v1/network/connect",.method=HTTP_POST,.handler=sta_connect_post},{.uri="/api/v1/network/forget",.method=HTTP_POST,.handler=sta_forget_post},{.uri="/api/v1/system/reboot",.method=HTTP_POST,.handler=reboot_post}};
 for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++){e=httpd_register_uri_handler(s_server,&routes[i]);if(e!=ESP_OK)return e;}e=files_web_register(s_server);if(e!=ESP_OK)return e;e=platform_web_register(s_server);if(e!=ESP_OK)return e;ESP_LOGI(TAG,"Admin platform ready: modular UI + files + health + logs + config backup");return ESP_OK;}
