#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app.h"

static const char *TAG = "CS003_CAM";
static camera_hw_state_t s_state;
static SemaphoreHandle_t s_lock;
static bool s_active;
static unsigned s_photos,s_errors,s_last_bytes,s_last_ms;

void camera_hw_get_state(camera_hw_state_t *out)
{
    if (out) *out = s_state;
}

esp_err_t camera_hw_probe(void)
{
    memset(&s_state, 0, sizeof(s_state));
    if(!s_lock)s_lock=xSemaphoreCreateMutex();
    if(!s_lock)return ESP_ERR_NO_MEM;
    s_state.probed = true;
    s_state.xclk_hz = 20000000;
    s_state.fb_count = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 ? 2 : 1;
    strlcpy(s_state.fb_location, s_state.fb_count == 2 ? "PSRAM" : "DRAM", sizeof(s_state.fb_location));
    strlcpy(s_state.sensor_name, "NOT DETECTED", sizeof(s_state.sensor_name));
    strlcpy(s_state.error, "initializing", sizeof(s_state.error));

    camera_config_t cfg = {
        .pin_pwdn = 17,
        .pin_reset = -1,
        .pin_xclk = 8,
        .pin_sccb_sda = 21,
        .pin_sccb_scl = 16,
        .pin_d7 = 2,
        .pin_d6 = 7,
        .pin_d5 = 10,
        .pin_d4 = 14,
        .pin_d3 = 11,
        .pin_d2 = 15,
        .pin_d1 = 13,
        .pin_d0 = 12,
        .pin_vsync = 6,
        .pin_href = 4,
        .pin_pclk = 9,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = s_state.fb_count,
        .fb_location = s_state.fb_count == 2 ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .sccb_i2c_port = -1
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        snprintf(s_state.error, sizeof(s_state.error), "%s (0x%x)", esp_err_to_name(err), (unsigned)err);
        ESP_LOGE(TAG, "OV5640 init/probe failed: %s", s_state.error);
        return err;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor) {
        strlcpy(s_state.error, "sensor handle unavailable", sizeof(s_state.error));
        ESP_LOGE(TAG, "%s", s_state.error);
        return ESP_FAIL;
    }

    s_state.pid = sensor->id.PID;
    s_state.midh = sensor->id.MIDH;
    s_state.midl = sensor->id.MIDL;
    s_state.ver = sensor->id.VER;
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&sensor->id);
    if (info && info->name) strlcpy(s_state.sensor_name, info->name, sizeof(s_state.sensor_name));
    else snprintf(s_state.sensor_name, sizeof(s_state.sensor_name), "PID %04X", s_state.pid);

    s_state.expected = (s_state.pid == OV5640_PID);
    s_state.ready = true;
    s_active = true;
    strlcpy(s_state.error, "none", sizeof(s_state.error));
    ESP_LOGI(TAG, "Camera probe OK sensor=%s PID=0x%04X MID=%02X%02X VER=%02X XCLK=%u fb=%u %s",
             s_state.sensor_name, s_state.pid, s_state.midh, s_state.midl, s_state.ver,
             (unsigned)s_state.xclk_hz, (unsigned)s_state.fb_count, s_state.fb_location);
    return ESP_OK;
}

/* Stable probe remains untouched; do not capture automatically.
 * Camera is initialized as it was in CS010Z, and is deinitialized on OFF. */
static camera_config_t default_camera_config(void)
{
    camera_config_t cfg = {
        .pin_pwdn = 17,
        .pin_reset = -1,
        .pin_xclk = 8,
        .pin_sccb_sda = 21,
        .pin_sccb_scl = 16,
        .pin_d7 = 2,
        .pin_d6 = 7,
        .pin_d5 = 10,
        .pin_d4 = 14,
        .pin_d3 = 11,
        .pin_d2 = 15,
        .pin_d1 = 13,
        .pin_d0 = 12,
        .pin_vsync = 6,
        .pin_href = 4,
        .pin_pclk = 9,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = s_state.fb_count,
        .fb_location = s_state.fb_count == 2 ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .sccb_i2c_port = -1
    };


    return cfg;
}
void photo_state_get(photo_state_t *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(!s_lock||xSemaphoreTake(s_lock,pdMS_TO_TICKS(80))!=pdTRUE)return;
    out->supported=s_state.ready&&s_state.expected;
    out->enabled=s_active;out->photos=s_photos;out->errors=s_errors;
    out->last_bytes=s_last_bytes;out->last_ms=s_last_ms;
    xSemaphoreGive(s_lock);
}
esp_err_t photo_power(bool on)
{
    if(!s_lock||xSemaphoreTake(s_lock,pdMS_TO_TICKS(100))!=pdTRUE)return ESP_ERR_TIMEOUT;
    esp_err_t e=ESP_OK;
    if(on&&!s_active){
        if(!s_state.ready||!s_state.expected)e=ESP_ERR_INVALID_STATE;
        else{camera_config_t cfg=default_camera_config();e=esp_camera_init(&cfg);
             if(e==ESP_OK)s_active=true;}
    }else if(!on&&s_active){
        e=esp_camera_deinit();
        if(e==ESP_OK)s_active=false;
    }
    xSemaphoreGive(s_lock);
    if(e==ESP_OK)platform_log_event("INFO","photo",on?"Camera READY":"Camera OFF buffers released");
    return e;
}
esp_err_t photo_take(httpd_req_t *request,char *saved,size_t saved_cap)
{
    if(saved&&saved_cap)saved[0]=0;
    if(!s_lock||xSemaphoreTake(s_lock,pdMS_TO_TICKS(100))!=pdTRUE)return ESP_ERR_TIMEOUT;
    if(!s_active){xSemaphoreGive(s_lock);return ESP_ERR_INVALID_STATE;}
    int64_t begun=esp_timer_get_time();
    camera_fb_t *fb=esp_camera_fb_get();
    esp_err_t e=ESP_FAIL;
    if(fb&&fb->buf&&fb->len&&fb->format==PIXFORMAT_JPEG){
        if(request){
            httpd_resp_set_type(request,"image/jpeg");
            httpd_resp_set_hdr(request,"Cache-Control","no-store");
            e=httpd_resp_send(request,(const char*)fb->buf,(ssize_t)fb->len);
        }else{
            const char *root=storage_volume_root("internal");
            if(root&&storage_lock(300)){
                char folder[64],path[120],name[32];
                snprintf(folder,sizeof(folder),"%s/photos",root);
                if(mkdir(folder,0755)==0||errno==EEXIST){
                    for(unsigned id=1;id<=1000000;id++){
                        snprintf(name,sizeof(name),"photo_%06u.jpg",id);
                        snprintf(path,sizeof(path),"%s/%s",folder,name);
                        struct stat st;
                        if(stat(path,&st)==0)continue;
                        if(errno!=ENOENT)break;
                        FILE *f=fopen(path,"wb");
                        if(!f)break;
                        size_t n=fwrite(fb->buf,1,fb->len,f);
                        int flush=fflush(f),closed=fclose(f);
                        if(n==fb->len&&flush==0&&closed==0){
                            e=ESP_OK;
                            if(saved&&saved_cap)snprintf(saved,saved_cap,"/photos/%s",name);
                        }else unlink(path);
                        break;
                    }
                }
                storage_unlock();
            }
        }
        if(e==ESP_OK){
            s_photos++;s_last_bytes=(unsigned)fb->len;
            s_last_ms=(unsigned)((esp_timer_get_time()-begun)/1000);
        }
    }
    if(fb)esp_camera_fb_return(fb);
    if(e!=ESP_OK)s_errors++;
    xSemaphoreGive(s_lock);
    platform_log_event(e==ESP_OK?"INFO":"ERROR","photo",
                       e==ESP_OK?"Manual JPEG captured":"Manual JPEG failed");
    return e;
}
static esp_err_t photo_status(httpd_req_t *r)
{
    photo_state_t p={0};photo_state_get(&p);
    char json[420];
    int n=snprintf(json,sizeof(json),
      "{\"supported\":%s,\"enabled\":%s,\"photos\":%u,\"errors\":%u,"
      "\"last_bytes\":%u,\"last_ms\":%u,\"psram_free\":%u}",
      p.supported?"true":"false",p.enabled?"true":"false",
      (unsigned)p.photos,(unsigned)p.errors,
      (unsigned)p.last_bytes,(unsigned)p.last_ms,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if(n<0||n>=(int)sizeof(json))return ESP_FAIL;
    httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_send(r,json,n);
}
static esp_err_t photo_toggle(httpd_req_t *r)
{
    esp_err_t e=photo_power(strstr(r->uri,"/on")!=NULL);
    if(e!=ESP_OK)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));
    return photo_status(r);
}
static esp_err_t photo_capture_http(httpd_req_t *r)
{
    esp_err_t e=photo_take(r,NULL,0);
    if(e==ESP_ERR_INVALID_STATE)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"CAMERA OFF");
    if(e==ESP_ERR_TIMEOUT)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"CAMERA BUSY");
    return e;
}
esp_err_t photo_web_register(httpd_handle_t h)
{
    const httpd_uri_t routes[]={
      {.uri="/api/v1/photo/state",.method=HTTP_GET,.handler=photo_status},
      {.uri="/api/v1/photo/on",.method=HTTP_POST,.handler=photo_toggle},
      {.uri="/api/v1/photo/off",.method=HTTP_POST,.handler=photo_toggle},
      {.uri="/api/v1/photo/take",.method=HTTP_POST,.handler=photo_capture_http}
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++){
        esp_err_t e=auth_register_uri_handler(h,&routes[i]);
        if(e!=ESP_OK)return e;
    }
    return ESP_OK;
}
