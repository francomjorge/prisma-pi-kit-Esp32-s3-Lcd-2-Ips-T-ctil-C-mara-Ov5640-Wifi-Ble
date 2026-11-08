#!/usr/bin/env python3
"""CI regression: black theme and page-dot positions from validated PRISMA-PI Lite."""
from pathlib import Path
import re

s = Path("main/display.c").read_text(encoding="utf-8")
model = Path("main/ui_model.h").read_text(encoding="utf-8")
old_ref = ("PRISMA-PI LITE: black 4/7/12, white 240/245/250, "
           "cyan 35/220/220, dim 45/60/75; "
           "palette blue/purple/pink/orange/green")

def function_body(name):
    match = re.search(r"static\s+(?:void|uint16_t)\s+" + re.escape(name) +
                      r"\([^)]*\)\s*\{", s)
    assert match, f"missing function {name}"
    start = match.end()
    depth = 1
    i = start
    while depth and i < len(s):
        if s[i] == "{": depth += 1
        elif s[i] == "}": depth -= 1
        i += 1
    assert depth == 0, f"unclosed function {name}"
    return s[start:i-1]

assert "UI_PAGE_COUNT = 2" in model
assert "UI_PAGE_ADMIN = 0" in model and "UI_PAGE_HOME = 1" in model
assert '#define LCD_H 320' in s and '#define LCD_W 240' in s
# Hidden gate has no main-carousel dots.
assert "page_marks(" not in s
assert "LCD_NAV_PHOTO" in s and "render_photo()" in s
assert "CAMERA ON" in s and "TAKE PHOTO" in s and "CAMERA OFF" in s
assert "nav=LCD_NAV_PHOTO" in s
assert "photo_power(true)" in s and "photo_take(NULL" in s
assert "rect(111,304,6,6" in s and "rect(125,304,6,6" in s
assert "settings_dots(" in s
assert '#define SETTINGS_COUNT 5' in s
assert 'settings_dots(page)' in s
gate = function_body("render_gate")
assert 'centered_button(132,48,"CONFIG")' in gate
assert 'centered_button(210,48,"BACK TO PRISMA")' in gate
assert "settings_dots(" not in gate
admin = function_body("render_settings")
for label in ("DEVICE","NETWORK","STORAGE","SECURITY","SYSTEM"):
    assert f'"{label}"' in admin
assert 'settings_footer(page)' in admin
assert 'RESET NETWORK' in admin and 'RESET ADMIN PASS' in admin
assert 'BACK TO PRISMA' in function_body('settings_footer')
assert 'CONFIRM REQUIRED' in function_body('render_confirm')
assert "UNIQUE HOSTNAME" in s and "MDNS ADDRESS" in s and "DIRECT AP ADDRESS" in s
assert "char host_lcd[40]" in s
assert "if(host_lcd[i]>=\'a\'&&host_lcd[i]<=\'z\')" in s
assert '"%s.LOCAL",host_lcd' in s
assert 'text(6,134,mdns_lcd,1,wh)' in s
assert "discovery_get_state(&discovery)" in s
assert "net.sta_connected&&net.sta_ip[0]" in s
swipe=Path("main/touch_hw.c").read_text(encoding="utf-8")
gesture=Path("main/touch_gesture.h").read_text(encoding="utf-8")
assert "#define TOUCH_SWIPE_MIN_PX 34" not in swipe
assert "TOUCHZ_SWIPE_PX=29" in gesture
assert "TOUCHZ_ACTION_GUARD_MS=260" in gesture
assert "TOUCHZ_CONTACT_MIN_MS=65" in gesture
assert "touchz_feed(&s_gesture" in swipe
assert 'network_forget_sta()' in s
assert 'auth_factory_reset()' in s

palette = function_body("moving_color")
for color in ("{30,120,255}", "{175,55,255}", "{255,35,150}",
              "{255,120,25}", "{45,225,120}"):
    assert color in palette, f"old palette changed: {color}"
assert "% 5U" in palette
assert "rgb(4,7,12)" in function_body("render_home")
assert "rgb(240,245,250)" in function_body("render_home")
assert "vivid_blue" not in s and "rgb(0,119,228)" not in s
assert "rgb(4,7,12)" in function_body("render_gate")
assert "rgb(4,7,12)" in function_body("render_settings")
assert "rgb(4,7,12)" in function_body("render_confirm")
print("PASS: hidden gate, two buttons, five admin settings pages and right-side PHOTO, confirmed local reset actions, black PRISMA palette")
