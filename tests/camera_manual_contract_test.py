#!/usr/bin/env python3
"""CI preflight for CS011B manual, explicitly controlled OV5640 capture."""
from pathlib import Path
import re

c=Path("main/camera_hw.c").read_text()
web=Path("main/web.c").read_text()
html=Path("main/web/camera-synth.html").read_text()
lcd=Path("main/display.c").read_text()
admin=Path("main/web/admin.js").read_text()
health=Path("main/platform_api.c").read_text()

assert "esp_camera_init(&cfg)" in c
assert "err = stop_driver_locked();" in c  # Camera releases probe buffers at boot.
assert "esp_camera_deinit()" in c
assert "esp_camera_fb_get()" in c
assert '{.uri="/api/v1/camera/snapshot",.method=HTTP_POST' in c
assert c.count("esp_camera_fb_return(fb)") >= 2
assert 'if (!s_driver_active)' in c  # No implicit photo capture while camera OFF.
assert "camera_hw_capture_to_storage" in c
assert "storage_lock(" in c and "storage_unlock()" in c
assert "xSemaphoreTake(s_lock" in c and "xSemaphoreGive(s_lock)" in c
assert "camera_web_register" in web
assert '\"capture_enabled\\":%s' in web or '\"capture_enabled\":%s' in web
for route in ("/api/v1/camera/runtime","/api/v1/camera/enable",
              "/api/v1/camera/disable","/api/v1/camera/snapshot"):
    assert route in c, route
assert "camera_runtime.enabled" in web and health
assert "CAMERA ON" in lcd and "TAKE PHOTO" in lcd and "CAMERA OFF" in lcd
assert "camera_hw_capture_to_storage" in lcd
assert "SETTINGS_COUNT 6" in lcd
assert "takePhoto" in html and "CAMERA OFF" in html
assert "URL.revokeObjectURL" in html
assert "setCamera(" in html and "readCamera()" in html
assert "capture_enabled?'READY':'OFF'" in admin
assert "fetch('/api/v1/camera/snapshot')" not in html  # No automatic snapshot
assert "fetch('/api/v1/camera/runtime')" not in html  # Runtime goes through controlled request wrapper.
print("PASS: manual photo only, camera OFF on boot, deinit buffers, shared Web/LCD controls")
