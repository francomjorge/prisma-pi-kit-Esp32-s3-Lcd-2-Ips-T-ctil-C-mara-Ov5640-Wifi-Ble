#!/usr/bin/env python3
from pathlib import Path
photo=Path("main/camera_hw.c").read_text()
storage=Path("main/storage.c").read_text()
net=Path("main/network.c").read_text()
assert "result=esp_camera_deinit();" in photo
assert "result=esp_camera_init(&cfg);" in photo
assert "esp_err_t recovered=esp_camera_init(&recovery);" in photo
assert "photo_jpeg_size(fb,&width,&height)" in photo
assert "width==s_sizes[s_size_index].width" in photo
assert "height==s_sizes[s_size_index].height" in photo
assert "e=save_photo(fb,path,sizeof(path));" in photo
assert "format_if_mount_failed = false" in storage, "DO NOT change boot storage initialization"
assert "internal_partition_is_blank" not in storage, "unsafe CS012D boot logic must not return"
assert "sta_wanted()?WIFI_MODE_APSTA:WIFI_MODE_AP" in net, "preserve working CS012C Wi-Fi"
print("PASS CS012E: on-demand camera only, original storage startup + stable Wi-Fi preserved")
