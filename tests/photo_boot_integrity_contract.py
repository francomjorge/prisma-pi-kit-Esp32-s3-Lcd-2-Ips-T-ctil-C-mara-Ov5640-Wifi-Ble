#!/usr/bin/env python3
from pathlib import Path
c=Path('main/camera_hw.c').read_text()
s=Path('main/storage.c').read_text()
w=Path('main/web/camera-synth.html').read_text()
assert 'static bool internal_partition_is_blank(void)' in s
assert 'ESP_PARTITION_SUBTYPE_DATA_FAT' in s
assert 'for(size_t offset=0;offset<partition->size;offset+=sizeof(page))' in s
assert 'if(page[j]!=0xFF)return false' in s
assert 'cfg.format_if_mount_failed=blank' in s
assert 'cfg.format_if_mount_failed = false' not in s or 'format_if_mount_failed = false' in s
assert 'ensure_photo_folder();' in s
assert 'mkdir("/internal/photos",0755)' in s
assert 'esp_camera_deinit()' in c
assert 'camera_config_t cfg=default_camera_config();' in c
assert 'esp_err_t restore=esp_camera_init(&old_cfg);' in c
assert 'photo_jpeg_dimensions(fb,&actual_width,&actual_height)' in c
assert 'actual_width==s_sizes[s_size_index].width' in c
assert 'actual_height==s_sizes[s_size_index].height' in c
assert 'e=save_photo(fb,path,sizeof(path));' in c
assert 'ÚLTIMA FOTO' in w and 'TIEMPO DE CAPTURA' in w
print('PASS CS012D: erased-only INTERNAL first boot, no destructive remount, fresh full-size valid JPEG')
