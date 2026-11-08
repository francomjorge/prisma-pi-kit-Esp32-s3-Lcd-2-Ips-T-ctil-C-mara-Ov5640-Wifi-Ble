#!/usr/bin/env python3
from pathlib import Path
c=Path("main/camera_hw.c").read_text()
h=Path("main/web/camera-synth.html").read_text()
d=Path("main/display.c").read_text()
admin=Path("main/web/admin.js").read_text()
assert "s_state.fb_count = 1;" in c
assert "save_photo(fb,path,sizeof(path))" in c
assert c.index("e=save_photo(fb,path,sizeof(path))") < c.index('httpd_resp_send(request,(const char*)fb->buf')
assert 'httpd_resp_set_hdr(request,"X-PRISMA-Photo-Path",path)' in c
assert "if(fb)esp_camera_fb_return(fb);" in c
assert "storage_volume_root(\"internal\")" in c
assert "storage_lock(1500)" in c and "storage_unlock()" in c
assert "fwrite(fb->buf,1,fb->len,file)" in c
assert "fflush(file)" in c and "fclose(file)" in c
assert "if(saved&&saved_cap)strlcpy(saved,path,saved_cap)" in c
assert "loadPhotos()" in h
assert "files/list?volume=internal&path=%2Fphotos" in h
assert "X-PRISMA-Photo-Path" in h
assert "photo_folder=1#FILES" in h
assert "URL.revokeObjectURL" in h
assert "photo_folder" in admin and "pane.internal.path='/photos'" in admin
assert "SAVED %.22s" in d and "ERROR 0x%X" in d
assert "s_last_photo_tap_ms" in d
print("PASS CS011S: shared persistent JPEGs; gallery, deep link, release and LCD feedback")
