#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"

static const char *TAG = "CS011B_CAMERA";
static camera_hw_state_t s_state;
static SemaphoreHandle_t s_lock;
static bool s_driver_active;
static uint32_t s_capture_count;
static uint32_t s_capture_failures;
static uint32_t s_last_jpeg_bytes;
static uint32_t s_last_capture_ms;
static char s_runtime_error[64] = "none";

/* All camera consumers must enter through this one owner. One framebuffer
 * is enough for a manually requested still; continuous modes come later. */
static camera_config_t camera_config_default(void)
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
        .fb_count = 1,
        .fb_location = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ?
                        CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .sccb_i2c_port = -1
    };
    return cfg;
}

void camera_hw_get_state(camera_hw_state_t *out)
{
    if (out) *out = s_state; /* Inventory is fixed after boot probe. */
}

static esp_err_t start_driver_locked(void)
{
    if (s_driver_active) return ESP_OK;
    if (!s_state.ready || !s_state.expected) return ESP_ERR_INVALID_STATE;
    camera_config_t cfg = camera_config_default();
    esp_err_t e = esp_camera_init(&cfg);
    if (e == ESP_OK) {
        s_driver_active = true;
        strlcpy(s_runtime_error, "none", sizeof(s_runtime_error));
        platform_log_event("INFO", "camera", "Camera driver enabled; idle until manual snapshot");
    } else {
        snprintf(s_runtime_error, sizeof(s_runtime_error), "%s", esp_err_to_name(e));
        platform_log_event("ERROR", "camera", "Camera enable failed");
    }
    return e;
}

static esp_err_t stop_driver_locked(void)
{
    if (!s_driver_active) return ESP_OK;
    esp_err_t e = esp_camera_deinit();
    if (e == ESP_OK) {
        s_driver_active = false;
        strlcpy(s_runtime_error, "none", sizeof(s_runtime_error));
        platform_log_event("INFO", "camera", "Camera disabled; driver and buffers released");
    } else {
        snprintf(s_runtime_error, sizeof(s_runtime_error), "%s", esp_err_to_name(e));
        platform_log_event("ERROR", "camera", "Camera deinit failed");
    }
    return e;
}

esp_err_t camera_hw_probe(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.probed = true;
    s_state.xclk_hz = 20000000;
    s_state.fb_count = 1;
    strlcpy(s_state.fb_location,
            heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? "PSRAM" : "DRAM",
            sizeof(s_state.fb_location));
    strlcpy(s_state.sensor_name, "NOT DETECTED", sizeof(s_state.sensor_name));
    strlcpy(s_state.error, "initializing", sizeof(s_state.error));

    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    camera_config_t cfg = camera_config_default();
    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        snprintf(s_state.error, sizeof(s_state.error), "%s", esp_err_to_name(err));
        ESP_LOGE(TAG, "OV5640 probe failed: %s", s_state.error);
        return err;
    }
    s_driver_active = true;
    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor) {
        strlcpy(s_state.error, "sensor handle unavailable", sizeof(s_state.error));
        (void)stop_driver_locked();
        return ESP_FAIL;
    }

    s_state.pid = sensor->id.PID;
    s_state.midh = sensor->id.MIDH;
    s_state.midl = sensor->id.MIDL;
    s_state.ver = sensor->id.VER;
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&sensor->id);
    if (info && info->name)
        strlcpy(s_state.sensor_name, info->name, sizeof(s_state.sensor_name));
    else
        snprintf(s_state.sensor_name, sizeof(s_state.sensor_name), "PID %04X", s_state.pid);
    s_state.expected = (s_state.pid == OV5640_PID);
    s_state.ready = true;
    strlcpy(s_state.error, "none", sizeof(s_state.error));

    /* Probe is inventory ONLY: no capture or allocated camera framebuffers
     * survive startup. The operator explicitly ENABLES capture later. */
    err = stop_driver_locked();
    if (err != ESP_OK) {
        strlcpy(s_state.error, "probe camera deinit failed", sizeof(s_state.error));
        s_state.ready = false;
        return err;
    }
    ESP_LOGI(TAG, "OV5640 probe OK sensor=%s PID=%04X; camera OFF, PSRAM released",
             s_state.sensor_name, s_state.pid);
    return ESP_OK;
}

void camera_runtime_get_state(camera_runtime_state_t *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!s_lock || xSemaphoreTake(s_lock,pdMS_TO_TICKS(100))!=pdTRUE) return;
    out->supported=s_state.ready&&s_state.expected;
    out->enabled=s_driver_active;
    out->captured=s_capture_count;
    out->failed=s_capture_failures;
    out->last_jpeg_bytes=s_last_jpeg_bytes;
    out->last_capture_ms=s_last_capture_ms;
    xSemaphoreGive(s_lock);
}

esp_err_t camera_hw_set_enabled(bool enabled)
{
    if (!s_lock || xSemaphoreTake(s_lock,pdMS_TO_TICKS(250))!=pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t e=enabled?start_driver_locked():stop_driver_locked();
    xSemaphoreGive(s_lock);
    return e;
}

esp_err_t camera_hw_capture_to_storage(char *output_name,size_t capacity)
{
    if (output_name && capacity) output_name[0]=0;
    if (!output_name || capacity<32) return ESP_ERR_INVALID_ARG;
    if (!s_lock || xSemaphoreTake(s_lock,pdMS_TO_TICKS(250))!=pdTRUE)
        return ESP_ERR_TIMEOUT;
    if (!s_driver_active) {xSemaphoreGive(s_lock);return ESP_ERR_INVALID_STATE;}
    const char *root=storage_volume_root("internal");
    if (!root || !storage_lock(1500)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    uint64_t t0=(uint64_t)esp_timer_get_time();
    camera_fb_t *fb=esp_camera_fb_get();
    esp_err_t result=ESP_OK;
    if (!fb || !fb->buf || fb->format!=PIXFORMAT_JPEG || !fb->len) {
        result=ESP_FAIL;
    } else {
        char folder[128],path[170],name[36];
        strlcpy(folder,root,sizeof(folder));
        strlcat(folder,"/snapshots",sizeof(folder));
        if(mkdir(folder,0755)!=0 && errno!=EEXIST) result=ESP_FAIL;
        /* Preserve shots across reboots; never overwrite a previous JPEG.
         * Storage operations are serialized by storage_lock. */
        bool available=false;
        for(uint32_t candidate=s_capture_count+1U;result==ESP_OK&&candidate<1000000U;candidate++){
            snprintf(name,sizeof(name),"shot_%010lu.jpg",(unsigned long)candidate);
            strlcpy(path,folder,sizeof(path));
            strlcat(path,"/",sizeof(path));
            strlcat(path,name,sizeof(path));
            struct stat existing;
            if(stat(path,&existing)!=0){
                if(errno==ENOENT){available=true;break;}
                result=ESP_FAIL;
            }
        }
        if(!available)result=ESP_FAIL;
        if(result==ESP_OK){
            FILE *file=fopen(path,"wb");
            if(!file) result=ESP_FAIL;
            else {
                size_t written=fwrite(fb->buf,1,fb->len,file);
                if(written!=fb->len || fflush(file)!=0) result=ESP_FAIL;
                if(fclose(file)!=0) result=ESP_FAIL;
                if(result!=ESP_OK) unlink(path);
            }
            if(result==ESP_OK){
                ++s_capture_count;
                s_last_jpeg_bytes=(uint32_t)fb->len;
                s_last_capture_ms=(uint32_t)(((uint64_t)esp_timer_get_time()-t0)/1000U);
                strlcpy(output_name,"/snapshots/",capacity);
                strlcat(output_name,name,capacity);
            }
        }
    }
    if(fb)esp_camera_fb_return(fb);
    if(result!=ESP_OK)++s_capture_failures;
    storage_unlock();
    xSemaphoreGive(s_lock);
    platform_log_event(result==ESP_OK?"INFO":"ERROR","camera",
        result==ESP_OK?"LCD manual photo saved to internal storage":
                       "LCD manual photo failed");
    return result;
}

static esp_err_t runtime_state_get(httpd_req_t *req)
{
    bool active = false;
    uint32_t count=0,fail=0,bytes=0,ms=0;
    char error[64];
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "camera state unavailable");
    active=s_driver_active;count=s_capture_count;fail=s_capture_failures;
    bytes=s_last_jpeg_bytes;ms=s_last_capture_ms;
    strlcpy(error,s_runtime_error,sizeof(error));
    xSemaphoreGive(s_lock);
    char json[580];
    int n=snprintf(json,sizeof(json),
        "{\"schema\":\"prisma.camera.v1\",\"supported\":%s,\"enabled\":%s,"
        "\"actual\":\"%s\",\"capture_count\":%lu,\"failures\":%lu,"
        "\"last_jpeg_bytes\":%lu,\"last_capture_ms\":%lu,"
        "\"psram_free_bytes\":%lu,\"last_error\":\"%s\","
        "\"snapshot_url\":\"/api/v1/camera/snapshot.jpg\"}",
        s_state.ready && s_state.expected ? "true" : "false",
        active?"true":"false",active?"READY":"OFF",
        (unsigned long)count,(unsigned long)fail,(unsigned long)bytes,
        (unsigned long)ms,
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),error);
    if (n<0 || n>=(int)sizeof(json))
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"camera JSON overflow");
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,json,n);
}

static esp_err_t camera_mode_post(httpd_req_t *req)
{
    bool enable = (strstr(req->uri, "/enable") != NULL);
    if (!s_lock || xSemaphoreTake(s_lock,pdMS_TO_TICKS(100))!=pdTRUE)
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"camera busy");
    esp_err_t e=enable?start_driver_locked():stop_driver_locked();
    xSemaphoreGive(s_lock);
    if(e!=ESP_OK)
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));
    return runtime_state_get(req);
}

static esp_err_t snapshot_get(httpd_req_t *req)
{
    if (!s_lock || xSemaphoreTake(s_lock,pdMS_TO_TICKS(100))!=pdTRUE)
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"camera busy");
    if (!s_driver_active) {
        xSemaphoreGive(s_lock);
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                                   "Camera OFF; press ENABLE CAMERA first");
    }
    uint64_t t0=(uint64_t)esp_timer_get_time();
    camera_fb_t *fb=esp_camera_fb_get();
    if (!fb || !fb->buf || !fb->len || fb->format!=PIXFORMAT_JPEG) {
        if (fb) esp_camera_fb_return(fb);
        ++s_capture_failures;
        strlcpy(s_runtime_error,"invalid or unavailable JPEG framebuffer",
                sizeof(s_runtime_error));
        xSemaphoreGive(s_lock);
        platform_log_event("ERROR","camera","Manual JPEG capture failed");
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "camera snapshot failed");
    }
    const uint32_t latency=(uint32_t)(((uint64_t)esp_timer_get_time()-t0)/1000U);
    ++s_capture_count;
    s_last_jpeg_bytes=(uint32_t)fb->len;
    s_last_capture_ms=latency;
    httpd_resp_set_type(req,"image/jpeg");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_send(req,(const char *)fb->buf,(ssize_t)fb->len);
    esp_camera_fb_return(fb); /* ALWAYS give driver buffer back. */
    xSemaphoreGive(s_lock);
    platform_log_event(result==ESP_OK?"INFO":"ERROR","camera",
                       result==ESP_OK?"Manual JPEG captured":"Manual JPEG send failed");
    return result;
}

esp_err_t camera_web_register(httpd_handle_t server)
{
    const httpd_uri_t endpoints[]={
        {.uri="/api/v1/camera/runtime",.method=HTTP_GET,.handler=runtime_state_get},
        {.uri="/api/v1/camera/enable",.method=HTTP_POST,.handler=camera_mode_post},
        {.uri="/api/v1/camera/disable",.method=HTTP_POST,.handler=camera_mode_post},
        {.uri="/api/v1/camera/snapshot.jpg",.method=HTTP_GET,.handler=snapshot_get},
    };
    for(size_t i=0;i<sizeof(endpoints)/sizeof(endpoints[0]);i++){
        esp_err_t e=auth_register_uri_handler(server,&endpoints[i]);
        if(e!=ESP_OK) return e;
    }
    return ESP_OK;
}
