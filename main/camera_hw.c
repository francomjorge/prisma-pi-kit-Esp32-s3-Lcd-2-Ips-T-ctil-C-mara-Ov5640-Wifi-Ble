#include <stdio.h>
#include <string.h>
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app.h"

static const char *TAG = "CS003_CAM";
static camera_hw_state_t s_state;

void camera_hw_get_state(camera_hw_state_t *out)
{
    if (out) *out = s_state;
}

esp_err_t camera_hw_probe(void)
{
    memset(&s_state, 0, sizeof(s_state));
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
    strlcpy(s_state.error, "none", sizeof(s_state.error));
    ESP_LOGI(TAG, "Camera probe OK sensor=%s PID=0x%04X MID=%02X%02X VER=%02X XCLK=%u fb=%u %s",
             s_state.sensor_name, s_state.pid, s_state.midh, s_state.midl, s_state.ver,
             (unsigned)s_state.xclk_hz, (unsigned)s_state.fb_count, s_state.fb_location);
    return ESP_OK;
}
