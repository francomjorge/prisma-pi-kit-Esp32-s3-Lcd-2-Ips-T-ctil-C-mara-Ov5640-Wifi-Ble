#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "app.h"

static const char *TAG = "CS005_STORAGE";
static const char *INTERNAL_MOUNT = "/internal";
static const char *INTERNAL_LABEL = "storage";
static const char *SD_MOUNT = "/sdcard";
static storage_state_t s_state;
static SemaphoreHandle_t s_fs_mutex;
static sdmmc_card_t *s_sd_card;

static void init_volume(storage_volume_state_t *v, const char *id, const char *fs, const char *mount)
{
    memset(v, 0, sizeof(*v));
    v->available = true;
    v->writable = true;
    v->max_open_files = PRISMA_STORAGE_MAX_OPEN_FILES;
    strlcpy(v->id, id, sizeof(v->id));
    strlcpy(v->fs, fs, sizeof(v->fs));
    strlcpy(v->mount, mount, sizeof(v->mount));
    strlcpy(v->error, "not mounted", sizeof(v->error));
}

static void refresh_internal(void)
{
    if (!s_state.internal.mounted) return;
    size_t total = 0, used = 0;
    esp_err_t e = esp_spiffs_info(INTERNAL_LABEL, &total, &used);
    if (e == ESP_OK) {
        s_state.internal.total_bytes = total;
        s_state.internal.used_bytes = used;
        s_state.internal.free_bytes = total >= used ? total - used : 0;
        strlcpy(s_state.internal.error, "none", sizeof(s_state.internal.error));
    } else {
        snprintf(s_state.internal.error, sizeof(s_state.internal.error), "info: %s", esp_err_to_name(e));
    }
}

static void refresh_sd(void)
{
    if (!s_state.sd.mounted) return;
    uint64_t total = 0, free_bytes = 0;
    esp_err_t e = esp_vfs_fat_info(SD_MOUNT, &total, &free_bytes);
    if (e == ESP_OK) {
        s_state.sd.total_bytes = total;
        s_state.sd.free_bytes = free_bytes;
        s_state.sd.used_bytes = total >= free_bytes ? total - free_bytes : 0;
        strlcpy(s_state.sd.error, "none", sizeof(s_state.sd.error));
    } else {
        snprintf(s_state.sd.error, sizeof(s_state.sd.error), "info: %s", esp_err_to_name(e));
    }
}

static esp_err_t mount_internal(void)
{
    if (s_state.internal.mounted) return ESP_OK;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = INTERNAL_MOUNT,
        .partition_label = INTERNAL_LABEL,
        .max_files = PRISMA_STORAGE_MAX_OPEN_FILES,
        .format_if_mount_failed = false
    };
    esp_err_t e = esp_vfs_spiffs_register(&conf);
    if (e != ESP_OK) {
        snprintf(s_state.internal.error, sizeof(s_state.internal.error), "mount: %s", esp_err_to_name(e));
        ESP_LOGW(TAG, "Internal SPIFFS not mounted: %s", esp_err_to_name(e));
        return e;
    }
    s_state.internal.mounted = true;
    refresh_internal();
    ESP_LOGI(TAG, "Internal SPIFFS mounted total=%llu free=%llu",
             (unsigned long long)s_state.internal.total_bytes,
             (unsigned long long)s_state.internal.free_bytes);
    return ESP_OK;
}

static esp_err_t mount_sd(void)
{
    if (s_state.sd.mounted) return ESP_OK;
    esp_err_t e = board_spi_start();
    s_state.shared_spi_ready = board_spi_ready();
    if (e != ESP_OK) {
        snprintf(s_state.sd.error, sizeof(s_state.sd.error), "spi: %s", esp_err_to_name(e));
        return e;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = GPIO_NUM_41;
    slot.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = PRISMA_STORAGE_MAX_OPEN_FILES,
        .allocation_unit_size = 16 * 1024
    };

    e = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mount_cfg, &s_sd_card);
    if (e != ESP_OK) {
        snprintf(s_state.sd.error, sizeof(s_state.sd.error), "mount: %s", esp_err_to_name(e));
        ESP_LOGW(TAG, "microSD not mounted: %s", esp_err_to_name(e));
        return e;
    }

    s_state.sd.mounted = true;
    refresh_sd();
    if (s_sd_card) {
        ESP_LOGI(TAG, "microSD mounted name=%s capacity=%lluMB free=%lluMB",
                 s_sd_card->cid.name,
                 (unsigned long long)(s_state.sd.total_bytes / (1024ULL * 1024ULL)),
                 (unsigned long long)(s_state.sd.free_bytes / (1024ULL * 1024ULL)));
    }
    return ESP_OK;
}

esp_err_t storage_start(void)
{
    init_volume(&s_state.internal, "internal", "SPIFFS", INTERNAL_MOUNT);
    init_volume(&s_state.sd, "sd", "FAT/SDSPI", SD_MOUNT);
    s_fs_mutex = xSemaphoreCreateMutex();
    if (!s_fs_mutex) return ESP_ERR_NO_MEM;

    esp_err_t spi = board_spi_start();
    s_state.shared_spi_ready = board_spi_ready();
    if (spi != ESP_OK) ESP_LOGW(TAG, "Shared SPI init failed: %s", esp_err_to_name(spi));

    esp_err_t internal = mount_internal();
    esp_err_t sd = mount_sd();
    if (internal != ESP_OK && sd != ESP_OK) return ESP_FAIL;
    return ESP_OK;
}

void storage_get_state(storage_state_t *out)
{
    if (!out) return;
    refresh_internal();
    refresh_sd();
    s_state.shared_spi_ready = board_spi_ready();
    *out = s_state;
}

esp_err_t storage_internal_initialize(void)
{
    if (!storage_lock(3000)) return ESP_ERR_TIMEOUT;
    if (s_state.internal.mounted) {
        esp_vfs_spiffs_unregister(INTERNAL_LABEL);
        s_state.internal.mounted = false;
    }
    esp_err_t e = esp_spiffs_format(INTERNAL_LABEL);
    if (e == ESP_OK) e = mount_internal();
    storage_unlock();
    return e;
}

esp_err_t storage_sd_retry(void)
{
    if (s_state.sd.mounted) return ESP_OK;
    if (!storage_lock(3000)) return ESP_ERR_TIMEOUT;
    esp_err_t e = mount_sd();
    storage_unlock();
    return e;
}

const char *storage_volume_root(const char *volume)
{
    if (!volume) return NULL;
    if (!strcmp(volume, "internal") && s_state.internal.mounted) return INTERNAL_MOUNT;
    if (!strcmp(volume, "sd") && s_state.sd.mounted) return SD_MOUNT;
    return NULL;
}

bool storage_volume_mounted(const char *volume)
{
    return storage_volume_root(volume) != NULL;
}

bool storage_lock(uint32_t timeout_ms)
{
    return s_fs_mutex && xSemaphoreTake(s_fs_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void storage_unlock(void)
{
    if (s_fs_mutex) xSemaphoreGive(s_fs_mutex);
}
