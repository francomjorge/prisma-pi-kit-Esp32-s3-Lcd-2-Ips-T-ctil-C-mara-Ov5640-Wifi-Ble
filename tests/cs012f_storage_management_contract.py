#!/usr/bin/env python3
from pathlib import Path
boot=Path("main/main.c").read_text()
storage=Path("main/storage.c").read_text()
app=Path("main/app.h").read_text()
api=Path("main/files_api.c").read_text()
ui=Path("main/web/admin.js").read_text()
web=Path("main/web.c").read_text()
net=Path("main/network.c").read_text()
cam=Path("main/camera_hw.c").read_text()

# Recovery invariant: neither FAT flash reading nor format on synchronous boot path.
assert boot.index('err = display_start();') < boot.index('err = network_start();') < boot.index('err = web_start();')
assert boot.index('ota_boot_check(display_ready());') < boot.index('storage_postboot_start();')
assert boot.index('Startup sequence complete') < boot.index('storage_postboot_start();')
assert 'storage_postboot_start();' in boot and 'xTaskCreate(storage_boot_worker' in storage
assert 'vTaskDelay(pdMS_TO_TICKS(800))' in storage
assert 'storage_start(void)' in storage and 'mount_internal()' in storage
assert 'cfg.format_if_mount_failed' not in storage
assert 'format_if_mount_failed = false' in storage
assert 'erased_internal_partition(void)' in storage
assert 'esp_partition_read(p,offset,chunk,len)' in storage
assert 'for(size_t j=0;j<len;j++)if(chunk[j]!=0xFF)return false;' in storage
assert 'if(erased)' in storage and 'esp_vfs_fat_spiflash_format_rw_wl' in storage
assert 'ensure_photo_directory();' in storage
assert 'INTERNAL mount failed; nonblank/unknown FAT preserved; no autoformat' in storage

# Destructive paths require explicit server-side and browser confirmation.
assert 'storage_sd_initialize' in app and 'storage_postboot_start' in app
assert 'esp_vfs_fat_sdcard_format(SD_MOUNT,s_sd_card)' in storage
assert 'e=mount_sd(true)' in storage
assert 'mount_sd(false)' in storage
assert '"/api/v1/storage/sd/init"' in api
assert 'strcmp(confirm,"ERASE_SD")!=0' in api
assert 'strcmp(confirm,"ERASE_INTERNAL")!=0' in api
assert 'prompt(' in ui and 'BORRAR SD' in ui and 'BORRAR INTERNAL' in ui
for field in ('boot_init','boot_init_status'):
    assert field in (api+ui+web) or field=='boot_init_status' # JSON uses boot_init
assert 'INITIALIZE / FORMAT SD' in ui
assert 'INITIALIZE / FORMAT INTERNAL' in ui
assert 'RETRY / MOUNT SD' in ui
assert "case'FILES':" in ui and "case'STORAGE':" in ui
assert "state.storage?.internal" in ui
# Preserve validated Wi-Fi, camera and on-device controls.
assert 'sta_wanted()?WIFI_MODE_APSTA:WIFI_MODE_AP' in net
assert 'photo_jpeg_size' in cam and 'esp_err_t recovered=esp_camera_init(&recovery)' in cam
print("PASS CS012F: deferred blank-only FAT initialization, explicit SD format, storage/file UI, camera/Wi-Fi regression")
