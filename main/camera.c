#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"

#define PREVIEW_JPEG_BUFFER_BYTES (384U * 1024U)

static const char *TAG = "CAMERA";
static SemaphoreHandle_t camera_lock;
static uint8_t *preview_jpeg_buffer;

bool camera_probed = false;
bool camera_ready = false;
bool camera_expected = false;
uint16_t camera_pid = 0;
uint8_t camera_midh = 0, camera_midl = 0, camera_ver = 0;
char camera_sensor_name[24] = "NOT PROBED";
char camera_error[64] = "probing";

bool camera_capture_attempted = false;
bool camera_capture_ready = false;
uint16_t camera_frame_width = 0, camera_frame_height = 0;
uint32_t camera_frame_bytes = 0, camera_capture_ms = 0;
char camera_frame_format[12] = "pending";
char camera_capture_error[64] = "not attempted";

bool camera_preview_active = false;
bool camera_preview_staging = false;
uint32_t camera_preview_frames = 0;
uint32_t camera_preview_capture_ms = 0;
uint32_t camera_preview_copy_ms = 0;
uint32_t camera_preview_decode_ms = 0;
uint32_t camera_preview_frame_ms = 0;
uint16_t camera_preview_fps = 0;
char camera_preview_error[64] = "not started";

uint8_t camera_jpeg_quality = 10, camera_fb_count = 1;
char camera_grab_mode[16] = "when-empty";

const char *camera_state_text(void) {
    if (!camera_probed) return "probing";
    return camera_ready ? "ready" : "error";
}

const char *camera_capture_state_text(void) {
    if (!camera_capture_attempted) return "pending";
    return camera_capture_ready ? "ready" : "error";
}

const char *camera_preview_state_text(void) {
    if (camera_preview_active) return "active";
    if (camera_preview_frames > 0) return "ready";
    if (camera_capture_ready) return "available";
    if (camera_capture_attempted) return "unavailable";
    return "pending";
}

void camera_preview_set_active(bool active) {
    camera_preview_active = active;
}

static const char *format_name(pixformat_t f) {
    switch (f) {
        case PIXFORMAT_RGB565: return "RGB565";
        case PIXFORMAT_YUV422: return "YUV422";
        case PIXFORMAT_GRAYSCALE: return "GRAY";
        case PIXFORMAT_JPEG: return "JPEG";
        case PIXFORMAT_RGB888: return "RGB888";
        case PIXFORMAT_RAW: return "RAW";
        case PIXFORMAT_RGB444: return "RGB444";
        case PIXFORMAT_RGB555: return "RGB555";
        default: return "UNKNOWN";
    }
}

static bool lock_camera(void) {
    return camera_lock && xSemaphoreTake(camera_lock, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void unlock_camera(void) {
    if (camera_lock) xSemaphoreGive(camera_lock);
}

esp_err_t camera_capture_test(void) {
    camera_capture_attempted = true;
    camera_capture_ready = false;
    camera_frame_width = 0;
    camera_frame_height = 0;
    camera_frame_bytes = 0;
    camera_capture_ms = 0;
    strlcpy(camera_frame_format, "pending", sizeof(camera_frame_format));

    if (!camera_ready) {
        strlcpy(camera_capture_error, "camera not initialized", sizeof(camera_capture_error));
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock_camera()) {
        strlcpy(camera_capture_error, "camera busy", sizeof(camera_capture_error));
        return ESP_ERR_TIMEOUT;
    }

    int64_t start = esp_timer_get_time();
    camera_fb_t *fb = esp_camera_fb_get();
    camera_capture_ms = (uint32_t)((esp_timer_get_time() - start + 999) / 1000);
    if (!fb) {
        unlock_camera();
        strlcpy(camera_capture_error, "frame unavailable", sizeof(camera_capture_error));
        ESP_LOGE(TAG, "Capture test failed: %s", camera_capture_error);
        return ESP_FAIL;
    }

    camera_frame_width = (uint16_t)fb->width;
    camera_frame_height = (uint16_t)fb->height;
    camera_frame_bytes = (uint32_t)fb->len;
    strlcpy(camera_frame_format, format_name(fb->format), sizeof(camera_frame_format));
    esp_camera_fb_return(fb);
    unlock_camera();

    camera_capture_ready = true;
    strlcpy(camera_capture_error, "none", sizeof(camera_capture_error));
    ESP_LOGI(TAG, "Capture test OK: %ux%u %s %u bytes in %u ms",
             camera_frame_width, camera_frame_height, camera_frame_format,
             (unsigned)camera_frame_bytes, (unsigned)camera_capture_ms);
    return ESP_OK;
}

esp_err_t camera_preview_frame(uint8_t *out, size_t out_bytes) {
    if (!camera_ready || !camera_capture_ready) {
        strlcpy(camera_preview_error, "camera/frame test not ready", sizeof(camera_preview_error));
        return ESP_ERR_INVALID_STATE;
    }
    if (!out || out_bytes < (320U * 240U * 2U)) {
        strlcpy(camera_preview_error, "preview buffer too small", sizeof(camera_preview_error));
        return ESP_ERR_INVALID_SIZE;
    }
    if (!lock_camera()) {
        strlcpy(camera_preview_error, "camera busy", sizeof(camera_preview_error));
        return ESP_ERR_TIMEOUT;
    }

    int64_t start = esp_timer_get_time();
    camera_fb_t *fb = esp_camera_fb_get();
    int64_t captured = esp_timer_get_time();
    camera_preview_capture_ms = (uint32_t)((captured - start + 999) / 1000);
    if (!fb) {
        unlock_camera();
        strlcpy(camera_preview_error, "preview frame unavailable", sizeof(camera_preview_error));
        return ESP_FAIL;
    }

    if (fb->format != PIXFORMAT_JPEG || fb->width != 640 || fb->height != 480) {
        snprintf(camera_preview_error, sizeof(camera_preview_error), "unexpected %ux%u %s",
                 (unsigned)fb->width, (unsigned)fb->height, format_name(fb->format));
        esp_camera_fb_return(fb);
        unlock_camera();
        return ESP_ERR_INVALID_RESPONSE;
    }

    camera_frame_bytes = (uint32_t)fb->len;
    bool ok = false;
    int64_t decode_start = 0;
    int64_t done = 0;

    if (preview_jpeg_buffer) {
        if (fb->len > PREVIEW_JPEG_BUFFER_BYTES) {
            esp_camera_fb_return(fb);
            unlock_camera();
            strlcpy(camera_preview_error, "preview jpeg exceeds staging buffer", sizeof(camera_preview_error));
            return ESP_ERR_INVALID_SIZE;
        }

        size_t jpeg_len = fb->len;
        int64_t copy_start = esp_timer_get_time();
        memcpy(preview_jpeg_buffer, fb->buf, jpeg_len);
        int64_t copied = esp_timer_get_time();
        camera_preview_copy_ms = (uint32_t)((copied - copy_start + 999) / 1000);

        esp_camera_fb_return(fb);
        unlock_camera();

        decode_start = esp_timer_get_time();
        ok = jpg2rgb565(preview_jpeg_buffer, jpeg_len, out, JPG_SCALE_2X);
        done = esp_timer_get_time();
    } else {
        camera_preview_copy_ms = 0;
        decode_start = esp_timer_get_time();
        ok = jpg2rgb565(fb->buf, fb->len, out, JPG_SCALE_2X);
        done = esp_timer_get_time();
        esp_camera_fb_return(fb);
        unlock_camera();
    }

    camera_preview_decode_ms = (uint32_t)((done - decode_start + 999) / 1000);
    camera_preview_frame_ms = (uint32_t)((done - start + 999) / 1000);
    camera_preview_fps = camera_preview_frame_ms ? (uint16_t)(1000U / camera_preview_frame_ms) : 0;

    if (!ok) {
        strlcpy(camera_preview_error, "jpeg decode failed", sizeof(camera_preview_error));
        return ESP_FAIL;
    }

    camera_preview_frames++;
    strlcpy(camera_preview_error, "none", sizeof(camera_preview_error));
    return ESP_OK;
}

esp_err_t camera_copy_jpeg(uint8_t *out, size_t out_bytes, size_t *jpeg_bytes,
                           uint16_t *width, uint16_t *height, uint32_t *capture_ms) {
    if (!camera_ready || !out || !jpeg_bytes) return ESP_ERR_INVALID_STATE;
    if (!lock_camera()) return ESP_ERR_TIMEOUT;

    int64_t start = esp_timer_get_time();
    camera_fb_t *fb = esp_camera_fb_get();
    uint32_t ms = (uint32_t)((esp_timer_get_time() - start + 999) / 1000);
    if (!fb) {
        unlock_camera();
        return ESP_FAIL;
    }
    if (fb->format != PIXFORMAT_JPEG) {
        esp_camera_fb_return(fb);
        unlock_camera();
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (fb->len > out_bytes) {
        esp_camera_fb_return(fb);
        unlock_camera();
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(out, fb->buf, fb->len);
    *jpeg_bytes = fb->len;
    if (width) *width = (uint16_t)fb->width;
    if (height) *height = (uint16_t)fb->height;
    if (capture_ms) *capture_ms = ms;
    camera_frame_bytes = (uint32_t)fb->len;

    esp_camera_fb_return(fb);
    unlock_camera();
    return ESP_OK;
}

esp_err_t camera_start(void) {
    bool psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
    camera_lock = xSemaphoreCreateMutex();
    if (!camera_lock) return ESP_ERR_NO_MEM;

    camera_config_t cfg = {
        .pin_pwdn = 17, .pin_reset = -1, .pin_xclk = 8,
        .pin_sccb_sda = 21, .pin_sccb_scl = 16,
        .pin_d7 = 2, .pin_d6 = 7, .pin_d5 = 10, .pin_d4 = 14,
        .pin_d3 = 11, .pin_d2 = 15, .pin_d1 = 13, .pin_d0 = 12,
        .pin_vsync = 6, .pin_href = 4, .pin_pclk = 9,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_VGA,
        .jpeg_quality = 10,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = -1
    };

    if (!psram) {
        cfg.fb_location = CAMERA_FB_IN_DRAM;
        cfg.fb_count = 1;
        cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }

    camera_jpeg_quality = (uint8_t)cfg.jpeg_quality;
    camera_fb_count = (uint8_t)cfg.fb_count;
    strlcpy(camera_grab_mode,
            cfg.grab_mode == CAMERA_GRAB_LATEST ? "latest" : "when-empty",
            sizeof(camera_grab_mode));

    strlcpy(camera_sensor_name, "NOT DETECTED", sizeof(camera_sensor_name));
    strlcpy(camera_error, "initializing", sizeof(camera_error));

    esp_err_t e = esp_camera_init(&cfg);
    if (e != ESP_OK) {
        camera_probed = true;
        snprintf(camera_error, sizeof(camera_error), "%s (0x%x)", esp_err_to_name(e), (unsigned)e);
        ESP_LOGE(TAG, "Init failed: %s", camera_error);
        return e;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        camera_probed = true;
        strlcpy(camera_error, "sensor handle unavailable", sizeof(camera_error));
        return ESP_FAIL;
    }

    camera_pid = s->id.PID;
    camera_midh = s->id.MIDH;
    camera_midl = s->id.MIDL;
    camera_ver = s->id.VER;
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&s->id);
    if (info && info->name) strlcpy(camera_sensor_name, info->name, sizeof(camera_sensor_name));
    else snprintf(camera_sensor_name, sizeof(camera_sensor_name), "PID %04X", camera_pid);

    if (psram) {
        preview_jpeg_buffer = heap_caps_malloc(PREVIEW_JPEG_BUFFER_BYTES,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        camera_preview_staging = preview_jpeg_buffer != NULL;
        if (!camera_preview_staging) {
            ESP_LOGW(TAG, "Preview staging allocation failed; fallback keeps camera lock during decode");
        }
    }

    camera_expected = (camera_pid == OV5640_PID);
    camera_ready = true;
    camera_probed = true;
    strlcpy(camera_error, "none", sizeof(camera_error));

    ESP_LOGI(TAG, "Sensor %s PID=0x%04X VGA JPEG q=%u fb=%u grab=%s staging=%s",
             camera_sensor_name, camera_pid, camera_jpeg_quality, camera_fb_count,
             camera_grab_mode, camera_preview_staging ? "yes" : "no");
    return ESP_OK;
}
