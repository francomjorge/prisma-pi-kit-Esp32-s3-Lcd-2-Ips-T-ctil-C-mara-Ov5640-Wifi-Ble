#!/usr/bin/env python3
from pathlib import Path
c=Path('main/camera_hw.c').read_text()
lcd=Path('main/display.c').read_text()
html=Path('main/web/camera-synth.html').read_text()
app=Path('main/app.h').read_text()
for x in ['FRAMESIZE_VGA','FRAMESIZE_SVGA','FRAMESIZE_XGA','FRAMESIZE_SXGA',
          'FRAMESIZE_UXGA','FRAMESIZE_QXGA','FRAMESIZE_5MP']:
 assert x in c
assert 'photo_set_options(uint8_t size_index,uint8_t quality_index)' in c
assert 'sensor->set_framesize(sensor,s_sizes[size_index].frame)' in c
assert 'sensor->set_quality(sensor,s_qualities[quality_index].driver_quality)' in c
assert 'size_index>=PHOTO_SIZE_COUNT||quality_index>=PHOTO_QUALITY_COUNT' in c
assert 'ESP_ERR_NOT_SUPPORTED' in c
assert 's_size_index=size_index;s_quality_index=quality_index' in c
assert 'out->size_index=s_size_index;out->quality_index=s_quality_index;' in c
assert 'photo_options_http' in c and '/api/v1/photo/options' in c
assert 'photo_set_options((uint8_t)((cfg.size_index+1U)%7U)' in lcd
assert 'photo_set_options(cfg.size_index,(uint8_t)((cfg.quality_index+1U)%3U))' in lcd
assert 'photo_state_get(&cfg)' in lcd
assert 'size_index,quality_index,jpeg_quality' in app
for x in ['PHOTO','LIVE','VIDEO','STREAM','RECORD','PROCESS','OVERLAYS','PRESETS','OUTPUT']:
 assert "['"+x+"'" in html
assert 'modules' in html and 'futureModule' in html
assert "call('/api/v1/photo/options?size='" in html
assert "'applyQuality').onclick=applyQuality" in html
assert '2592 × 1944' in html and '2048 × 1536' in html
assert 'Alta · 8' in html and 'Máxima · 5' in html
assert 'NO IMPLEMENTADO EN ESTE BUILD' in html
for x in ['HOME','ADMIN','CAMERA SYNTH']:
 assert x in html
print('PASS CS012B: nine module states, shared LCD/Web photo settings, 7 resolutions and 3 JPEG profiles')
