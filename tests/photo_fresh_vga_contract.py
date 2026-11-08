#!/usr/bin/env python3
from pathlib import Path
c=Path("main/camera_hw.c").read_text()
w=Path("main/web/camera-synth.html").read_text()
assert c.count(".frame_size = s_sizes[s_size_index].frame,")==2
assert c.count(".jpeg_quality = s_qualities[s_quality_index].driver_quality,")==2
assert c.count(".fb_count = s_state.fb_count,")==2
assert "s_state.fb_count = 1" in c
assert "camera_fb_t *discard=esp_camera_fb_get();" in c
assert "if(discard)esp_camera_fb_return(discard);" in c
assert "camera_fb_t *fb=discard?esp_camera_fb_get():NULL;" in c
assert c.index("if(discard)esp_camera_fb_return(discard);")<c.index("camera_fb_t *fb=discard?esp_camera_fb_get():NULL;")<c.index("e=save_photo(fb,path,sizeof(path));")
assert "if(fb)esp_camera_fb_return(fb);" in c
assert "800 × 600" in w
print("PASS CS011T: SVGA JPEG, one buffer, discard queued frame, capture fresh frame")
