#include <stdio.h>
#include <string.h>
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"

static const char *TAG="CAMERA";
bool camera_probed=false;
bool camera_ready=false;
bool camera_expected=false;
uint16_t camera_pid=0;
uint8_t camera_midh=0,camera_midl=0,camera_ver=0;
char camera_sensor_name[24]="NOT PROBED";
char camera_error[64]="probing";
bool camera_capture_attempted=false;
bool camera_capture_ready=false;
uint16_t camera_frame_width=0,camera_frame_height=0;
uint32_t camera_frame_bytes=0,camera_capture_ms=0;
char camera_frame_format[12]="pending";
char camera_capture_error[64]="not attempted";

const char *camera_state_text(void){
 if(!camera_probed)return "probing";
 return camera_ready?"ready":"error";
}

const char *camera_capture_state_text(void){
 if(!camera_capture_attempted)return "pending";
 return camera_capture_ready?"ready":"error";
}

static const char *format_name(pixformat_t f){
 switch(f){
  case PIXFORMAT_RGB565:return "RGB565";
  case PIXFORMAT_YUV422:return "YUV422";
  case PIXFORMAT_GRAYSCALE:return "GRAY";
  case PIXFORMAT_JPEG:return "JPEG";
  case PIXFORMAT_RGB888:return "RGB888";
  case PIXFORMAT_RAW:return "RAW";
  case PIXFORMAT_RGB444:return "RGB444";
  case PIXFORMAT_RGB555:return "RGB555";
  default:return "UNKNOWN";
 }
}

esp_err_t camera_capture_test(void){
 camera_capture_attempted=true;
 camera_capture_ready=false;
 camera_frame_width=0;
 camera_frame_height=0;
 camera_frame_bytes=0;
 camera_capture_ms=0;
 strlcpy(camera_frame_format,"pending",sizeof(camera_frame_format));
 if(!camera_ready){
  strlcpy(camera_capture_error,"camera not initialized",sizeof(camera_capture_error));
  return ESP_ERR_INVALID_STATE;
 }
 int64_t start=esp_timer_get_time();
 camera_fb_t *fb=esp_camera_fb_get();
 camera_capture_ms=(uint32_t)((esp_timer_get_time()-start+999)/1000);
 if(!fb){
  strlcpy(camera_capture_error,"frame unavailable",sizeof(camera_capture_error));
  ESP_LOGE(TAG,"Capture test failed: %s",camera_capture_error);
  return ESP_FAIL;
 }
 camera_frame_width=(uint16_t)fb->width;
 camera_frame_height=(uint16_t)fb->height;
 camera_frame_bytes=(uint32_t)fb->len;
 strlcpy(camera_frame_format,format_name(fb->format),sizeof(camera_frame_format));
 esp_camera_fb_return(fb);
 camera_capture_ready=true;
 strlcpy(camera_capture_error,"none",sizeof(camera_capture_error));
 ESP_LOGI(TAG,"Capture test OK: %ux%u %s %u bytes in %u ms",camera_frame_width,camera_frame_height,camera_frame_format,(unsigned)camera_frame_bytes,(unsigned)camera_capture_ms);
 return ESP_OK;
}

esp_err_t camera_start(void){
 camera_config_t cfg={
  .pin_pwdn=17,
  .pin_reset=-1,
  .pin_xclk=8,
  .pin_sccb_sda=21,
  .pin_sccb_scl=16,
  .pin_d7=2,
  .pin_d6=7,
  .pin_d5=10,
  .pin_d4=14,
  .pin_d3=11,
  .pin_d2=15,
  .pin_d1=13,
  .pin_d0=12,
  .pin_vsync=6,
  .pin_href=4,
  .pin_pclk=9,
  .xclk_freq_hz=20000000,
  .ledc_timer=LEDC_TIMER_0,
  .ledc_channel=LEDC_CHANNEL_0,
  .pixel_format=PIXFORMAT_JPEG,
  .frame_size=FRAMESIZE_QVGA,
  .jpeg_quality=12,
  .fb_count=1,
  .fb_location=CAMERA_FB_IN_PSRAM,
  .grab_mode=CAMERA_GRAB_WHEN_EMPTY,
  .sccb_i2c_port=-1
 };
 if(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)==0)cfg.fb_location=CAMERA_FB_IN_DRAM;
 strlcpy(camera_sensor_name,"NOT DETECTED",sizeof(camera_sensor_name));
 strlcpy(camera_error,"initializing",sizeof(camera_error));
 esp_err_t e=esp_camera_init(&cfg);
 if(e!=ESP_OK){
  camera_probed=true;
  snprintf(camera_error,sizeof(camera_error),"%s (0x%x)",esp_err_to_name(e),(unsigned)e);
  ESP_LOGE(TAG,"Init failed: %s",camera_error);
  return e;
 }
 sensor_t *s=esp_camera_sensor_get();
 if(!s){
  camera_probed=true;
  strlcpy(camera_error,"sensor handle unavailable",sizeof(camera_error));
  ESP_LOGE(TAG,"Driver initialized but sensor handle is unavailable");
  return ESP_FAIL;
 }
 camera_pid=s->id.PID;
 camera_midh=s->id.MIDH;
 camera_midl=s->id.MIDL;
 camera_ver=s->id.VER;
 camera_sensor_info_t *info=esp_camera_sensor_get_info(&s->id);
 if(info && info->name)strlcpy(camera_sensor_name,info->name,sizeof(camera_sensor_name));
 else snprintf(camera_sensor_name,sizeof(camera_sensor_name),"PID %04X",camera_pid);
 camera_expected=(camera_pid==OV5640_PID);
 camera_ready=true;
 camera_probed=true;
 strlcpy(camera_error,"none",sizeof(camera_error));
 ESP_LOGI(TAG,"Sensor %s PID=0x%04X MID=%02X%02X VER=%02X expected_OV5640=%s",camera_sensor_name,camera_pid,camera_midh,camera_midl,camera_ver,camera_expected?"yes":"no");
 return ESP_OK;
}
