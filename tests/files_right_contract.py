#!/usr/bin/env python3
from pathlib import Path
d=Path('main/display.c').read_text()
f=Path('main/files_lcd.c').read_text()
w=Path('main/web/camera-synth.html').read_text()
c=Path('main/camera_hw.c').read_text()
cmake=Path('main/CMakeLists.txt').read_text()
assert 'LCD_NAV_FILES' in d and 'nav=LCD_NAV_FILES' in d
assert 'case LCD_NAV_FILES: render_files();break;' in d
assert 's_files_offset+=FILES_LCD_ROWS' in d
assert 'files_parent()' in d and 'files_select(index)' in d
assert 'files_lcd_list(s_files_volume,s_files_path,s_files_offset,&s_files_page)' in d
assert 'storage_lock(1000)' in f and 'storage_unlock()' in f
assert 'storage_volume_root(volume)' in f
assert 'opendir(dirpath)' in f and 'readdir(dir)' in f
assert 'out->count<FILES_LCD_ROWS' in f and 'out->total=seen' in f
assert 'files_lcd.c' in cmake
assert 'FRAMESIZE_SVGA' in c and 's_qualities[s_quality_index].driver_quality' in c
assert '800 × 600' in w
assert "files/list?volume=internal&path=%2Fphotos" in w
print('CS012A PASS: right-side paginated FILES reads shared FAT, SVGA photo, Web parity')
