#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "app.h"
#define STREAM_PORT 81
#define STREAM_BUFFER_BYTES (384U*1024U)
#define PART_BOUNDARY "prismapcamera"
char app_ssid[32],app_password[16];
bool network_ready=false,stream_server_ready=false;
uint16_t stream_clients=0,stream_fps=0;
uint32_t stream_frames=0,stream_bytes=0,stream_frame_ms=0;
char stream_error[64]="not started";
static esp_netif_t *ap;
static SemaphoreHandle_t stream_guard;
extern const char html_start[] asm("_binary_admin_html_start");
extern const char html_end[] asm("_binary_admin_html_end");
static esp_err_t page(httpd_req_t *r){httpd_resp_set_type(r,"text/html; charset=utf-8");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_send(r,html_start,html_end-html_start-1);}
static esp_err_t unavailable(httpd_req_t *r,const char *message){httpd_resp_set_status(r,"503 Service Unavailable");httpd_resp_set_type(r,"text/plain");return httpd_resp_sendstr(r,message);}
static esp_err_t status(httpd_req_t *r){
 char b[5400],ip[20]="";esp_chip_info_t chip;esp_chip_info(&chip);uint32_t flash=0;esp_err_t fe=esp_flash_get_size(NULL,&flash);esp_netif_ip_info_t info;if(ap&&esp_netif_get_ip_info(ap,&info)==ESP_OK)snprintf(ip,sizeof(ip),IPSTR,IP2STR(&info.ip));wifi_sta_list_t stations={0};esp_wifi_ap_get_sta_list(&stations);
 int n=snprintf(b,sizeof(b),"{\"build\":\"%s\",\"model\":\"ESP32-S3\",\"cores\":%d,\"revision\":%d,\"flash_bytes\":%"PRIu32",\"flash_read_ok\":%s,\"psram_bytes\":%u,\"heap_free\":%u,\"heap_min\":%u,\"uptime_s\":%"PRId64",\"display\":%s,\"touch\":%s,\"touch_poll_ms\":%u,\"touch_confirm_samples\":%u,\"touch_swipe_threshold_px\":%u,\"touch_debounce_ms\":%u,\"touch_taps\":%u,\"touch_swipes\":%u,\"touch_rejected\":%u,\"ssid\":\"%s\",\"ip\":\"%s\",\"clients\":%d,\"stream_server\":%s,\"stream_port\":%u,\"stream_clients\":%u,\"stream_frames\":%u,\"stream_bytes\":%u,\"stream_frame_ms\":%u,\"stream_fps\":%u,\"stream_error\":\"%s\",\"camera\":\"%s\",\"camera_sensor\":\"%s\",\"camera_pid\":\"0x%04X\",\"camera_expected\":%s,\"camera_midh\":\"0x%02X\",\"camera_midl\":\"0x%02X\",\"camera_ver\":\"0x%02X\",\"camera_error\":\"%s\",\"camera_jpeg_quality\":%u,\"camera_fb_count\":%u,\"camera_grab_mode\":\"%s\",\"capture\":\"%s\",\"frame_width\":%u,\"frame_height\":%u,\"frame_bytes\":%u,\"frame_format\":\"%s\",\"capture_ms\":%u,\"capture_error\":\"%s\",\"preview\":\"%s\",\"preview_frames\":%u,\"preview_capture_ms\":%u,\"preview_decode_ms\":%u,\"preview_frame_ms\":%u,\"preview_pipeline_fps\":%u,\"preview_display_ms\":%u,\"preview_loop_ms\":%u,\"preview_fps\":%u,\"preview_error\":\"%s\",\"mdns\":\"pending\",\"esp_now\":\"pending\"}",APP_BUILD,chip.cores,chip.revision,flash,fe==ESP_OK?"true":"false",(unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),(unsigned)esp_get_free_heap_size(),(unsigned)esp_get_minimum_free_heap_size(),esp_timer_get_time()/1000000,display_ready()?"true":"false",touch_ready()?"true":"false",touch_poll_ms,touch_confirm_samples,touch_swipe_threshold_px,touch_debounce_ms,(unsigned)touch_taps,(unsigned)touch_swipes,(unsigned)touch_rejected,app_ssid,ip,stations.num,stream_server_ready?"true":"false",STREAM_PORT,stream_clients,(unsigned)stream_frames,(unsigned)stream_bytes,(unsigned)stream_frame_ms,stream_fps,stream_error,camera_state_text(),camera_sensor_name,camera_pid,camera_expected?"true":"false",camera_midh,camera_midl,camera_ver,camera_error,camera_jpeg_quality,camera_fb_count,camera_grab_mode,camera_capture_state_text(),camera_frame_width,camera_frame_height,(unsigned)camera_frame_bytes,camera_frame_format,(unsigned)camera_capture_ms,camera_capture_error,camera_preview_state_text(),(unsigned)camera_preview_frames,(unsigned)camera_preview_capture_ms,(unsigned)camera_preview_decode_ms,(unsigned)camera_preview_frame_ms,(unsigned)camera_preview_fps,(unsigned)ui_preview_display_ms,(unsigned)ui_preview_loop_ms,(unsigned)ui_preview_fps,camera_preview_error);
 if(n<0||n>=(int)sizeof(b))return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"status overflow");httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_send(r,b,n);
}
static esp_err_t stream(httpd_req_t *r){
 if(!camera_ready)return unavailable(r,"camera not ready");
 if(!stream_guard||xSemaphoreTake(stream_guard,0)!=pdTRUE)return unavailable(r,"stream busy");
 uint8_t *jpeg=heap_caps_malloc(STREAM_BUFFER_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!jpeg)jpeg=heap_caps_malloc(STREAM_BUFFER_BYTES,MALLOC_CAP_8BIT);
 if(!jpeg){xSemaphoreGive(stream_guard);strlcpy(stream_error,"stream buffer allocation failed",sizeof(stream_error));return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"no stream buffer");}
 stream_clients=1;strlcpy(stream_error,"none",sizeof(stream_error));httpd_resp_set_type(r,"multipart/x-mixed-replace;boundary=" PART_BOUNDARY);httpd_resp_set_hdr(r,"Cache-Control","no-store, no-cache, must-revalidate");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");
 static const char boundary[]="\r\n--" PART_BOUNDARY "\r\n";esp_err_t result=ESP_OK;
 while(1){int64_t start=esp_timer_get_time();size_t len=0;uint16_t w=0,h=0;uint32_t cap_ms=0;esp_err_t e=camera_copy_jpeg(jpeg,STREAM_BUFFER_BYTES,&len,&w,&h,&cap_ms);if(e!=ESP_OK){snprintf(stream_error,sizeof(stream_error),"camera %s",esp_err_to_name(e));result=e;break;}char hdr[144];int hn=snprintf(hdr,sizeof(hdr),"Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Resolution: %ux%u\r\nX-Capture-Ms: %u\r\n\r\n",(unsigned)len,w,h,(unsigned)cap_ms);if(hn<0||hn>=(int)sizeof(hdr)){result=ESP_FAIL;break;}if(httpd_resp_send_chunk(r,boundary,strlen(boundary))!=ESP_OK||httpd_resp_send_chunk(r,hdr,hn)!=ESP_OK||httpd_resp_send_chunk(r,(const char*)jpeg,len)!=ESP_OK){result=ESP_OK;break;}stream_frames++;stream_bytes+=(uint32_t)len;stream_frame_ms=(uint32_t)((esp_timer_get_time()-start+999)/1000);stream_fps=stream_frame_ms?(uint16_t)(1000U/stream_frame_ms):0;vTaskDelay(pdMS_TO_TICKS(1));}
 free(jpeg);stream_clients=0;if(result==ESP_OK)strlcpy(stream_error,"none",sizeof(stream_error));xSemaphoreGive(stream_guard);return result;
}
static esp_err_t network_start(void){
 uint8_t mac[6];ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP));snprintf(app_ssid,sizeof(app_ssid),"PRISMA-CAM-%02X%02X%02X",mac[3],mac[4],mac[5]);nvs_handle_t h;esp_err_t e=nvs_open("prisma",NVS_READWRITE,&h);if(e!=ESP_OK)return e;size_t len=sizeof(app_password);e=nvs_get_str(h,"ap_key",app_password,&len);if(e==ESP_ERR_NVS_NOT_FOUND){snprintf(app_password,sizeof(app_password),"P%08"PRIX32,esp_random());e=nvs_set_str(h,"ap_key",app_password);if(e==ESP_OK)e=nvs_commit(h);}nvs_close(h);if(e!=ESP_OK)return e;
 ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());ap=esp_netif_create_default_wifi_ap();wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));wifi_config_t cfg={0};strlcpy((char*)cfg.ap.ssid,app_ssid,sizeof(cfg.ap.ssid));strlcpy((char*)cfg.ap.password,app_password,sizeof(cfg.ap.password));cfg.ap.ssid_len=strlen(app_ssid);cfg.ap.channel=6;cfg.ap.max_connection=4;cfg.ap.authmode=WIFI_AUTH_WPA2_PSK;ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&cfg));ESP_ERROR_CHECK(esp_wifi_start());
 httpd_handle_t server=NULL;httpd_config_t hc=HTTPD_DEFAULT_CONFIG();hc.stack_size=7168;e=httpd_start(&server,&hc);if(e!=ESP_OK)return e;httpd_uri_t root={.uri="/",.method=HTTP_GET,.handler=page};ESP_ERROR_CHECK(httpd_register_uri_handler(server,&root));root.uri="/admin";ESP_ERROR_CHECK(httpd_register_uri_handler(server,&root));httpd_uri_t st={.uri="/api/status",.method=HTTP_GET,.handler=status};ESP_ERROR_CHECK(httpd_register_uri_handler(server,&st));network_ready=true;
 stream_guard=xSemaphoreCreateBinary();if(stream_guard)xSemaphoreGive(stream_guard);if(!stream_guard){strlcpy(stream_error,"stream guard allocation failed",sizeof(stream_error));return ESP_OK;}
 httpd_handle_t stream_server=NULL;httpd_config_t sc=HTTPD_DEFAULT_CONFIG();sc.server_port=STREAM_PORT;sc.ctrl_port=32769;sc.stack_size=8192;sc.max_open_sockets=2;sc.lru_purge_enable=true;e=httpd_start(&stream_server,&sc);if(e==ESP_OK){httpd_uri_t su={.uri="/stream",.method=HTTP_GET,.handler=stream};e=httpd_register_uri_handler(stream_server,&su);}if(e==ESP_OK){stream_server_ready=true;strlcpy(stream_error,"none",sizeof(stream_error));}else{snprintf(stream_error,sizeof(stream_error),"server %s",esp_err_to_name(e));ESP_LOGE("PRISMA","Stream server: %s",stream_error);}return ESP_OK;
}
void app_main(void){esp_err_t e=nvs_flash_init();if(e==ESP_OK)e=network_start();if(e!=ESP_OK)ESP_LOGE("PRISMA","Network/NVS: %s",esp_err_to_name(e));e=display_start();if(e!=ESP_OK)ESP_LOGE("PRISMA","Display: %s (web remains available)",esp_err_to_name(e));e=camera_start();if(e!=ESP_OK)ESP_LOGE("PRISMA","Camera probe: %s (administration remains available)",esp_err_to_name(e));else{e=camera_capture_test();if(e!=ESP_OK)ESP_LOGE("PRISMA","Camera capture test: %s (administration remains available)",esp_err_to_name(e));}}
