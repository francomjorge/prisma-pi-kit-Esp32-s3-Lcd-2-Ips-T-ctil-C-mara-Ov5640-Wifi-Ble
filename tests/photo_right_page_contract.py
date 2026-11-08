#!/usr/bin/env python3
from pathlib import Path
c=Path("main/camera_hw.c").read_text()
lcd=Path("main/display.c").read_text()
web=Path("main/web.c").read_text()
html=Path("main/web/camera-synth.html").read_text()
for route in ("/api/v1/photo/state","/api/v1/photo/on","/api/v1/photo/off","/api/v1/photo/take"):
    assert route in c
assert '{.uri="/api/v1/photo/take",.method=HTTP_POST' in c
assert "esp_camera_fb_return(fb)" in c and "esp_camera_fb_get()" in c
assert "esp_camera_deinit()" in c
assert "storage_volume_root(\"internal\")" in c
assert "storage_lock(1500)" in c and "storage_unlock();" in c
assert 'photo_web_register(s_server)' in web
assert "photo_state_get(&p)" in lcd
assert "nav=LCD_NAV_PHOTO" in lcd
assert "if(action==UI_EVENT_SWIPE_LEFT)" in lcd
assert "if(action==UI_EVENT_SWIPE_RIGHT)" in lcd
assert "photo_take(NULL,saved" in lcd
assert "call('/api/v1/photo/take',{method:'POST'" in html
assert "call('/api/v1/photo/state')" in html
assert "URL.revokeObjectURL" in html
assert "window.addEventListener('beforeunload',release)" in html
assert "takePhoto();" not in html
assert "camera_hw_probe();" not in html
print("PASS PHOTO: separate right page, manual on-demand POST, shared state, buffer return")
