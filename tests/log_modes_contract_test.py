#!/usr/bin/env python3
"""Static regression guards for first shared LCD/Web control: log modes."""
from pathlib import Path

api = Path("main/platform_api.c").read_text()
log = Path("main/platform_log.c").read_text()
header = Path("main/app.h").read_text()
ui = Path("main/web/admin.js").read_text()

for level in ("OFF", "ERROR", "INFO", "DEBUG"):
    assert f'"{level}"' in log
    assert f'"{level}"' in api
    assert f'value=\\"{level}\\"' not in ui  # not an escaped literal
    assert f'value="{level}"' in ui

assert "s_mode = PLATFORM_LOG_INFO" in log
assert "retained_at_mode(level, s_mode)" in log
assert "s_filtered" in log
assert '"/api/v1/logs/config"' in api
assert ".method=HTTP_GET, .handler=logs_config_get" in api
assert ".method=HTTP_POST, .handler=logs_config_post" in api
assert "platform_log_mode_set(mode)" in api
assert "await api('/api/v1/logs/config'" in ui
assert "async function setLogMode(" in ui
assert "const result=await Promise.all([api('/api/v1/logs'),api('/api/v1/logs/config')])" in ui
assert "PLATFORM_LOG_OFF" in header
print("PASS: runtime log modes, bidirectional browser control and shared API")
