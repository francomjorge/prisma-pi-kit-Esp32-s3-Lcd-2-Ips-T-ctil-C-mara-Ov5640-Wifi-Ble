#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
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
/* Runtime-owned photo settings shared by Web and LCD.
 * Resolution/quality are deliberately not persisted across a reboot yet. */
static uint8_t s_size_index=1; /* SVGA stable default */
static uint8_t s_quality_index=1; /* HIGH = jpeg_quality 8 */
typedef struct {
    const char *id;
    framesize_t frame;
    uint16_t width,height;
} photo_size_option_t;
static const photo_size_option_t s_sizes[]={
    {"VGA",FRAMESIZE_VGA,640,480},
    {"SVGA",FRAMESIZE_SVGA,800,600},
    {"XGA",FRAMESIZE_XGA,1024,768},
    {"SXGA",FRAMESIZE_SXGA,1280,1024},
    {"UXGA",FRAMESIZE_UXGA,1600,1200},
    {"QXGA",FRAMESIZE_QXGA,2048,1536},
    {"5MP",FRAMESIZE_5MP,2592,1944}
};
static const struct {const char *id;int driver_quality;} s_qualities[]={
    {"STANDARD",12},{"HIGH",8},{"MAX",5}
};
#define PHOTO_SIZE_COUNT (sizeof(s_sizes)/sizeof(s_sizes[0]))
#define PHOTO_QUALITY_COUNT (sizeof(s_qualities)/sizeof(s_qualities[0]))


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
    s_state.fb_count = 1; /* one-shot photo mode: never queue stale frames */
    strlcpy(s_state.fb_location, heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 ? "PSRAM" : "DRAM", sizeof(s_state.fb_location));
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
        .frame_size = s_sizes[s_size_index].frame,
        .jpeg_quality = s_qualities[s_quality_index].driver_quality,
        .fb_count = s_state.fb_count,
        .fb_location = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
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
        .frame_size = s_sizes[s_size_index].frame,
        .jpeg_quality = s_qualities[s_quality_index].driver_quality,
        .fb_count = s_state.fb_count,
        .fb_location = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
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
    out->size_index=s_size_index;out->quality_index=s_quality_index;
    out->width=s_sizes[s_size_index].width;out->height=s_sizes[s_size_index].height;
    out->jpeg_quality=s_qualities[s_quality_index].driver_quality;
    xSemaphoreGive(s_lock);
}

/* Framesize controls framebuffer allocation at esp_camera_init time.
 * A sensor-only resize can leave the JPEG DMA buffer too small, especially
 * for UXGA/5MP. Restart the driver with the requested geometry and roll
 * back to the previous configuration if initialization fails.
 */
esp_err_t photo_set_options(uint8_t size_index,uint8_t quality_index)
{
    if(size_index>=PHOTO_SIZE_COUNT||quality_index>=PHOTO_QUALITY_COUNT)
        return ESP_ERR_INVALID_ARG;
    if(size_index>1&&heap_caps_get_total_size(MALLOC_CAP_SPIRAM)==0)
        return ESP_ERR_NOT_SUPPORTED;
    if(!s_lock||xSemaphoreTake(s_lock,pdMS_TO_TICKS(2000))!=pdTRUE)
        return ESP_ERR_TIMEOUT;
    const uint8_t previous_size=s_size_index,previous_quality=s_quality_index;
    if(size_index==previous_size&&quality_index==previous_quality){
        xSemaphoreGive(s_lock);return ESP_OK;
    }
    esp_err_t result=ESP_OK;
    if(s_active){
        result=esp_camera_deinit();
        if(result==ESP_OK){
            s_active=false;
            s_size_index=size_index;
            s_quality_index=quality_index;
            camera_config_t cfg=default_camera_config();
            result=esp_camera_init(&cfg);
            if(result==ESP_OK){
                s_active=true;
            }else{
                /* Rollback: preserve a working camera rather than losing PHOTO. */
                s_size_index=previous_size;
                s_quality_index=previous_quality;
                camera_config_t old_cfg=default_camera_config();
                esp_err_t restore=esp_camera_init(&old_cfg);
                s_active=(restore==ESP_OK);
                if(restore!=ESP_OK)
                    platform_log_event("ERROR","camera","Camera rollback init failed; CAMERA OFF");
            }
        }
    }else{
        s_size_index=size_index;
        s_quality_index=quality_index;
    }
    xSemaphoreGive(s_lock);
    platform_log_event(result==ESP_OK?"INFO":"ERROR","photo",
        result==ESP_OK?"JPEG resolution reinitialized with matching framebuffer":"JPEG resolution change rejected; restored previous settings");
    return result;
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

/* The same JPEG is saved and returned to the Web. Never report success
 * until the file has been closed and its contents committed. */
static esp_err_t save_photo(camera_fb_t *fb,char *saved,size_t saved_cap)
{
    const char *root=storage_volume_root("internal");
    if(!root)return ESP_ERR_INVALID_STATE;
    if(!storage_lock(1500))return ESP_ERR_TIMEOUT;
    esp_err_t result=ESP_FAIL;
    char folder[64],path[120],name[32];
    snprintf(folder,sizeof(folder),"%s/photos",root);
    if(mkdir(folder,0755)!=0&&errno!=EEXIST)goto done;
    for(unsigned id=1;id<=1000000U;id++){
        snprintf(name,sizeof(name),"photo_%06u.jpg",id);
        snprintf(path,sizeof(path),"%s/%s",folder,name);
        struct stat existing;
        if(stat(path,&existing)==0)continue;
        if(errno!=ENOENT)break;
        FILE *file=fopen(path,"wb");
        if(!file)break;
        const size_t written=fwrite(fb->buf,1,fb->len,file);
        const int flushed=fflush(file);
        const int closed=fclose(file);
        if(written==fb->len&&flushed==0&&closed==0){
            result=ESP_OK;
            if(saved&&saved_cap)snprintf(saved,saved_cap,"/photos/%s",name);
        }else{
            (void)unlink(path);
        }
        break;
    }
done:
    storage_unlock();
    return result;
}

/* Verify encoded JPEG integrity and dimensions; never silently store a
 * corrupted, lower resolution or stale-format frame as a successful shot. */
static bool photo_jpeg_dimensions(const camera_fb_t *fb,uint16_t *width,uint16_t *height)
{
    if(!fb||!fb->buf||fb->len<8||fb->format!=PIXFORMAT_JPEG)return false;
    const uint8_t *b=fb->buf;
    const size_t n=fb->len;
    if(b[0]!=0xFF||b[1]!=0xD8||b[n-2]!=0xFF||b[n-1]!=0xD9)return false;
    size_t i=2;
    while(i+3<n){
        if(b[i++]!=0xFF) return false;
        while(i<n&&b[i]==0xFF)i++;
        if(i>=n)return false;
        uint8_t marker=b[i++];
        if(marker==0xD9||marker==0xDA)break;
        if(marker==0x01||(marker>=0xD0&&marker<=0xD7))continue;
        if(i+1>=n)return false;
        size_t length=((size_t)b[i]<<8)|b[i+1];
        if(length<2||length>n-i)return false;
        if((marker>=0xC0&&marker<=0xC3)||
           (marker>=0xC5&&marker<=0xC7)||
           (marker>=0xC9&&marker<=0xCB)){
            if(length<7)return false;
            *height=(uint16_t)(((uint16_t)b[i+3]<<8)|b[i+4]);
            *width=(uint16_t)(((uint16_t)b[i+5]<<8)|b[i+6]);
            return true;
        }
        i+=length;
    }
    return false;
}

esp_err_t photo_take(httpd_req_t *request,char *saved,size_t saved_cap)
{
    if(saved&&saved_cap)saved[0]=0;
    if(!s_lock||xSemaphoreTake(s_lock,pdMS_TO_TICKS(100))!=pdTRUE)return ESP_ERR_TIMEOUT;
    if(!s_active){xSemaphoreGive(s_lock);return ESP_ERR_INVALID_STATE;}
    const int64_t begun=esp_timer_get_time();
    /* WHEN_EMPTY may expose a frame produced before the button press.
     * Discard it and request a subsequent frame with the same single buffer.
     * Explicit snapshot only; no background capture loop. */
    camera_fb_t *discard=esp_camera_fb_get();
    if(discard)esp_camera_fb_return(discard);
    camera_fb_t *fb=discard?esp_camera_fb_get():NULL;
    esp_err_t e=ESP_FAIL;
    char path[48]={0};
    uint16_t actual_width=0,actual_height=0;
    bool valid=photo_jpeg_dimensions(fb,&actual_width,&actual_height);
    if(!valid||actual_width!=s_sizes[s_size_index].width||
       actual_height!=s_sizes[s_size_index].height){
        if(fb)esp_camera_fb_return(fb);
        fb=esp_camera_fb_get();
        actual_width=actual_height=0;
        valid=photo_jpeg_dimensions(fb,&actual_width,&actual_height);
    }
    if(valid&&actual_width==s_sizes[s_size_index].width&&
       actual_height==s_sizes[s_size_index].height){
        e=save_photo(fb,path,sizeof(path));
        if(e==ESP_OK&&request){
            /* Return the exact JPEG that was persisted; do not take a
             * second frame for the browser preview. */
            httpd_resp_set_type(request,"image/jpeg");
            httpd_resp_set_hdr(request,"Cache-Control","no-store");
            httpd_resp_set_hdr(request,"X-PRISMA-Photo-Path",path);
            e=httpd_resp_send(request,(const char*)fb->buf,(ssize_t)fb->len);
        }
        if(e==ESP_OK){
            ++s_photos;
            s_last_bytes=(unsigned)fb->len;
            s_last_ms=(unsigned)((esp_timer_get_time()-begun)/1000);
            if(saved&&saved_cap)strlcpy(saved,path,saved_cap);
        }
    }
    /* The capture buffer always returns to the camera driver. */
    if(fb)esp_camera_fb_return(fb);
    if(e!=ESP_OK)++s_errors;
    xSemaphoreGive(s_lock);
    platform_log_event(e==ESP_OK?"INFO":"ERROR","photo",
                       e==ESP_OK?"JPEG saved in INTERNAL/photos":"Manual capture/save failed");
    return e;
}
static esp_err_t photo_status(httpd_req_t *r)
{
    photo_state_t p={0};photo_state_get(&p);
    char json[670];
    int n=snprintf(json,sizeof(json),
      "{\"supported\":%s,\"enabled\":%s,\"photos\":%u,\"errors\":%u,"
      "\"last_bytes\":%u,\"last_ms\":%u,\"psram_free\":%u,"
      "\"size_index\":%u,\"quality_index\":%u,\"width\":%u,\"height\":%u,"
      "\"resolution\":\"%s\",\"jpeg_profile\":\"%s\",\"jpeg_quality\":%u}",
      p.supported?"true":"false",p.enabled?"true":"false",
      (unsigned)p.photos,(unsigned)p.errors,
      (unsigned)p.last_bytes,(unsigned)p.last_ms,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
      (unsigned)p.size_index,(unsigned)p.quality_index,
      (unsigned)p.width,(unsigned)p.height,
      s_sizes[p.size_index].id,s_qualities[p.quality_index].id,
      (unsigned)p.jpeg_quality);
    if(n<0||n>=(int)sizeof(json))return ESP_FAIL;
    httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_send(r,json,n);
}
static esp_err_t photo_options_http(httpd_req_t *r)
{
    char qs[96],sz[8],quality[8],tail;
    if(httpd_req_get_url_query_str(r,qs,sizeof(qs))!=ESP_OK ||
       httpd_query_key_value(qs,"size",sz,sizeof(sz))!=ESP_OK ||
       httpd_query_key_value(qs,"quality",quality,sizeof(quality))!=ESP_OK)
        return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"size and quality required");
    unsigned size_index=0,quality_index=0;
    if(sscanf(sz,"%u%c",&size_index,&tail)!=1||
       sscanf(quality,"%u%c",&quality_index,&tail)!=1||
       size_index>=PHOTO_SIZE_COUNT||quality_index>=PHOTO_QUALITY_COUNT)
        return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"invalid photo settings");
    esp_err_t e=photo_set_options((uint8_t)size_index,(uint8_t)quality_index);
    if(e!=ESP_OK)
        return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,
                   e==ESP_ERR_NOT_SUPPORTED?"High resolution requires PSRAM":"Sensor rejected setting or camera busy");
    return photo_status(r);
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
    if(e==ESP_ERR_INVALID_STATE)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"CAMERA OFF OR INTERNAL STORAGE UNAVAILABLE");
    if(e==ESP_ERR_TIMEOUT)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"CAMERA BUSY");
    if(e==ESP_FAIL)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"JPEG capture or storage write failed");
    if(e==ESP_ERR_INVALID_STATE)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"INTERNAL STORAGE NOT READY");
    return e;
}
esp_err_t photo_web_register(httpd_handle_t h)
{
    const httpd_uri_t routes[]={
      {.uri="/api/v1/photo/state",.method=HTTP_GET,.handler=photo_status},
      {.uri="/api/v1/photo/on",.method=HTTP_POST,.handler=photo_toggle},
      {.uri="/api/v1/photo/off",.method=HTTP_POST,.handler=photo_toggle},
      {.uri="/api/v1/photo/take",.method=HTTP_POST,.handler=photo_capture_http},
      {.uri="/api/v1/photo/options",.method=HTTP_POST,.handler=photo_options_http}
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++){
        esp_err_t e=auth_register_uri_handler(h,&routes[i]);
        if(e!=ESP_OK)return e;
    }
    return ESP_OK;
}
