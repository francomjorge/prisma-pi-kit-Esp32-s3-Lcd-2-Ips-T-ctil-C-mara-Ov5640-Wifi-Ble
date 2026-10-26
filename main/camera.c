#include <stdio.h>
#include <string.h>
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app.h"

static const char *TAG="CAMERA";
bool camera_probed=false;
bool camera_ready=false;
bool camera_expected=false;
uint16_t camera_pid=0;
uint8_t camera_midh=0,camera_midl=0,camera_ver=0;
char camera_sensor_name[24]="NOT PROBED";
char camera_error[64]="probing";

const char *camera_state_text(void){
 if(!camera_probed)return "probing";
 return camera_ready?"ready":"error";
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
