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

assert "UI_PAGE_COUNT = 2" in model, "expected two LCD pages"
assert "UI_PAGE_ADMIN = 0" in model
assert "UI_PAGE_HOME = 1" in model
assert "UI_PAGE_NETWORK" not in model and "UI_PAGE_SYSTEM" not in model
assert '#define LCD_H 320' in s and '#define LCD_W 240' in s
assert '#define MARKER_Y (LCD_H - 16)' in s

dots = function_body("page_marks")
assert "i < UI_PAGE_COUNT" in dots
assert "108 + (int)i * 16" in dots
assert "MARKER_Y, 8, 8" in dots
assert "i == active ? cyan : dim" in dots
assert "rgb(35,220,220)" in dots
assert "rgb(45,60,75)" in dots
assert [(108+i*16,304,8,8) for i in range(2)] == [
    (108,304,8,8), (124,304,8,8)
]

palette = function_body("moving_color")
for color in ("{30,120,255}", "{175,55,255}", "{255,35,150}",
              "{255,120,25}", "{45,225,120}"):
    assert color in palette, f"old palette changed: {color}"
assert "% 5U" in palette
assert "rgb(4,7,12)" in function_body("render_home")
assert "rgb(240,245,250)" in function_body("render_home")
assert "page_marks(UI_PAGE_HOME)" in function_body("render_home")
assert "vivid_blue" not in s and "rgb(0,119,228)" not in s
assert "page_marks(UI_PAGE_ADMIN)" in function_body("render_admin")
assert "render_network(" not in s and "render_system(" not in s
admin = function_body("render_admin")
assert "rect(0,0,LCD_W,LCD_H,rgb(4,7,12))" in admin
assert "rgb(35,220,220)" in admin
assert "NETWORK / SYSTEM" in admin and "FILES / SECURITY" in admin
print("PASS: CONFIG(left) HOME(right), two markers, black theme, exact legacy palette")
print(old_ref)
