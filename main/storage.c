#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include "esp_partition.h"
#include "freertos/task.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "wear_levelling.h"
#include "app.h"

static const char *TAG = "CS005_STORAGE";
static const char *INTERNAL_MOUNT = "/internal";
static const char *INTERNAL_LABEL = "storage";
static const char *SD_MOUNT = "/sdcard";

static storage_state_t s_state;
static SemaphoreHandle_t s_fs_mutex;
static wl_handle_t s_internal_wl = WL_INVALID_HANDLE;
static sdmmc_card_t *s_sd_card;
static bool s_postboot_started;

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

static void refresh_volume(storage_volume_state_t *v)
{
    if (!v->mounted) return;
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    esp_err_t e = esp_vfs_fat_info(v->mount, &total, &free_bytes);
    if (e == ESP_OK) {
        v->total_bytes = total;
        v->free_bytes = free_bytes;
        v->used_bytes = total >= free_bytes ? total - free_bytes : 0;
        strlcpy(v->error, "none", sizeof(v->error));
    } else {
        snprintf(v->error, sizeof(v->error), "info: %s", esp_err_to_name(e));
    }
}

static esp_vfs_fat_mount_config_t internal_mount_config(void)
{
    esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = PRISMA_STORAGE_MAX_OPEN_FILES,
        .allocation_unit_size = 4096
    };
    return cfg;
}

static esp_err_t mount_internal(void)
{
    if (s_state.internal.mounted) return ESP_OK;

    esp_vfs_fat_mount_config_t cfg = internal_mount_config();
    esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl(
        INTERNAL_MOUNT, INTERNAL_LABEL, &cfg, &s_internal_wl);
    if (e != ESP_OK) {
        s_internal_wl = WL_INVALID_HANDLE;
        snprintf(s_state.internal.error, sizeof(s_state.internal.error),
                 "mount: %s", esp_err_to_name(e));
        ESP_LOGW(TAG, "Internal FAT/WL not mounted: %s", esp_err_to_name(e));
        return e;
    }

    s_state.internal.mounted = true;
    strlcpy(s_state.internal_boot_status,"mounted",sizeof(s_state.internal_boot_status));
    refresh_volume(&s_state.internal);
    ESP_LOGI(TAG, "Internal FAT/WL mounted total=%lluMB free=%lluMB",
             (unsigned long long)(s_state.internal.total_bytes / (1024ULL * 1024ULL)),
             (unsigned long long)(s_state.internal.free_bytes / (1024ULL * 1024ULL)));
    return ESP_OK;
}

static esp_err_t mount_sd(bool allow_format)
{
    if (s_state.sd.mounted) return ESP_OK;

    esp_err_t e = board_spi_start();
    s_state.shared_spi_ready = board_spi_ready();
    if (e != ESP_OK) {
        snprintf(s_state.sd.error, sizeof(s_state.sd.error),
                 "spi: %s", esp_err_to_name(e));
        return e;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = GPIO_NUM_41;
    slot.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = allow_format,
        .max_files = PRISMA_STORAGE_MAX_OPEN_FILES,
        .allocation_unit_size = 16 * 1024
    };

    s_sd_card=NULL;
    e = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &cfg, &s_sd_card);
    if (e != ESP_OK) {
        snprintf(s_state.sd.error, sizeof(s_state.sd.error),
                 "mount: %s", esp_err_to_name(e));
        ESP_LOGW(TAG, "microSD not mounted: %s", esp_err_to_name(e));
        return e;
    }

    s_state.sd.mounted = true;
    refresh_volume(&s_state.sd);
    if (s_sd_card) {
        ESP_LOGI(TAG, "microSD mounted name=%s total=%lluMB free=%lluMB",
                 s_sd_card->cid.name,
                 (unsigned long long)(s_state.sd.total_bytes / (1024ULL * 1024ULL)),
                 (unsigned long long)(s_state.sd.free_bytes / (1024ULL * 1024ULL)));
    }
    return ESP_OK;
}

esp_err_t storage_start(void)
{
    init_volume(&s_state.internal, "internal", "FAT/WL", INTERNAL_MOUNT);
    init_volume(&s_state.sd, "sd", "FAT/SDSPI", SD_MOUNT);

    s_fs_mutex = xSemaphoreCreateMutex();
    if (!s_fs_mutex) return ESP_ERR_NO_MEM;
    strlcpy(s_state.internal_boot_status,"pending",sizeof(s_state.internal_boot_status));

    esp_err_t spi = board_spi_start();
    s_state.shared_spi_ready = board_spi_ready();
    if (spi != ESP_OK) {
        ESP_LOGW(TAG, "Shared SPI init failed: %s", esp_err_to_name(spi));
    }

    esp_err_t internal = mount_internal();
    esp_err_t sd = mount_sd(false);
    if (internal != ESP_OK && sd != ESP_OK) return ESP_FAIL;
    return ESP_OK;
}

void storage_get_state(storage_state_t *out)
{
    if (!out) return;
    refresh_volume(&s_state.internal);
    refresh_volume(&s_state.sd);
    s_state.shared_spi_ready = board_spi_ready();
    *out = s_state;
}

/* Run ONLY after LCD, Wi-Fi, HTTP and OTA boot check have finished.
 * CS012D read the entire FAT partition before LCD init and caused a black
 * screen on real hardware. Never repeat that work on the boot path.
 */
static void ensure_photo_directory(void)
{
    if(s_state.internal.mounted){
        if(mkdir("/internal/photos",0755)!=0&&errno!=EEXIST)
            ESP_LOGW(TAG,"photos directory unavailable errno=%d",errno);
    }
}

static bool erased_internal_partition(void)
{
    const esp_partition_t *p=esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_FAT,INTERNAL_LABEL);
    if(!p||p->size==0)return false;
    uint8_t chunk[1024];
    for(size_t offset=0;offset<p->size;offset+=sizeof(chunk)){
        size_t len=p->size-offset;
        if(len>sizeof(chunk))len=sizeof(chunk);
        if(esp_partition_read(p,offset,chunk,len)!=ESP_OK)return false;
        for(size_t j=0;j<len;j++)if(chunk[j]!=0xFF)return false;
        if((offset & 0x3FFFU)==0)vTaskDelay(pdMS_TO_TICKS(1)+1);
    }
    return true;
}

static void storage_boot_worker(void *arg)
{
    (void)arg;
    /* Neither flash scan nor FAT formatting may delay LCD initialization. */
    vTaskDelay(pdMS_TO_TICKS(800));
    if(!storage_lock(12000)){
        strlcpy(s_state.internal_boot_status,"busy",sizeof(s_state.internal_boot_status));
        platform_log_event("WARN","storage","INTERNAL first-boot check skipped: filesystem busy");
        vTaskDelete(NULL);
        return;
    }
    if(s_state.internal.mounted){
        ensure_photo_directory();
        strlcpy(s_state.internal_boot_status,"mounted",sizeof(s_state.internal_boot_status));
    }else{
        strlcpy(s_state.internal_boot_status,"checking",sizeof(s_state.internal_boot_status));
        const bool erased=erased_internal_partition();
        if(erased){
            esp_err_t e=esp_vfs_fat_spiflash_format_rw_wl(INTERNAL_MOUNT,INTERNAL_LABEL);
            if(e==ESP_OK)e=mount_internal();
            if(e==ESP_OK){
                ensure_photo_directory();
                strlcpy(s_state.internal_boot_status,"initialized",sizeof(s_state.internal_boot_status));
                platform_log_event("INFO","storage","Erased INTERNAL formatted after startup; photos ready");
            }else{
                strlcpy(s_state.internal_boot_status,"error",sizeof(s_state.internal_boot_status));
                snprintf(s_state.internal.error,sizeof(s_state.internal.error),"postboot init: %s",esp_err_to_name(e));
                platform_log_event("ERROR","storage","Blank INTERNAL format failed; preserve system boot");
            }
        }else{
            strlcpy(s_state.internal_boot_status,"preserved",sizeof(s_state.internal_boot_status));
            platform_log_event("WARN","storage","INTERNAL mount failed; nonblank/unknown FAT preserved; no autoformat");
        }
    }
    storage_unlock();
    vTaskDelete(NULL);
}

esp_err_t storage_postboot_start(void)
{
    if(!s_fs_mutex)return ESP_ERR_INVALID_STATE;
    if(s_postboot_started)return ESP_OK;
    s_postboot_started=true;
    if(xTaskCreate(storage_boot_worker,"storage_postboot",4096,NULL,tskIDLE_PRIORITY+1,NULL)!=pdPASS){
        s_postboot_started=false;
        strlcpy(s_state.internal_boot_status,"task_error",sizeof(s_state.internal_boot_status));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t storage_internal_initialize(void)
{
    if (!storage_lock(5000)) return ESP_ERR_TIMEOUT;

    if (s_state.internal.mounted) {
        esp_err_t unmount = esp_vfs_fat_spiflash_unmount_rw_wl(
            INTERNAL_MOUNT, s_internal_wl);
        if (unmount != ESP_OK) {
            storage_unlock();
            return unmount;
        }
        s_state.internal.mounted = false;
        s_internal_wl = WL_INVALID_HANDLE;
    }

    esp_err_t e = esp_vfs_fat_spiflash_format_rw_wl(
        INTERNAL_MOUNT, INTERNAL_LABEL);
    if (e == ESP_OK) e = mount_internal();
    if(e==ESP_OK){
        ensure_photo_directory();
        strlcpy(s_state.internal_boot_status,"manual",sizeof(s_state.internal_boot_status));
        platform_log_event("WARN","storage","INTERNAL formatted manually with explicit confirmation");
    }
    storage_unlock();
    return e;
}

esp_err_t storage_sd_retry(void)
{
    if (s_state.sd.mounted) return ESP_OK;
    if (!storage_lock(5000)) return ESP_ERR_TIMEOUT;
    esp_err_t e = mount_sd(false);
    storage_unlock();
    return e;
}

/* Destructive action; the web route requires the exact typed confirmation.
 * Never format microSD automatically on boot or after ordinary mount failures.
 */
esp_err_t storage_sd_initialize(void)
{
    if(!storage_lock(12000))return ESP_ERR_TIMEOUT;
    esp_err_t e;
    if(s_state.sd.mounted&&s_sd_card){
        e=esp_vfs_fat_sdcard_format(SD_MOUNT,s_sd_card);
        if(e==ESP_OK)refresh_volume(&s_state.sd);
        else{
            s_state.sd.mounted=false;
            s_sd_card=NULL;
            snprintf(s_state.sd.error,sizeof(s_state.sd.error),"format: %s",esp_err_to_name(e));
        }
    }else{
        e=mount_sd(true);
    }
    storage_unlock();
    if(e==ESP_OK)platform_log_event("WARN","storage","microSD explicitly formatted by administrator");
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
    return s_fs_mutex &&
           xSemaphoreTake(s_fs_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void storage_unlock(void)
{
    if (s_fs_mutex) xSemaphoreGive(s_fs_mutex);
}
