#!/usr/bin/env python3
from pathlib import Path
n=Path('main/network.c').read_text()
h=Path('main/app.h').read_text()
w=Path('main/web.c').read_text()
a=Path('main/web/admin.js').read_text()
assert 'sta_wanted()?WIFI_MODE_APSTA:WIFI_MODE_AP' in n
assert 'esp_wifi_set_mode(WIFI_MODE_AP)' in n
assert 'WIFI_EVENT_AP_STADISCONNECTED' in n
assert 'WIFI_EVENT_AP_STOP' in n
assert 's_ap_last_reason=event?event->reason:0' in n
assert 's_connect_pending=true' in n and 's_connect_pending=false' in n
assert 'if(s_state.sta_connected || s_connect_pending) return ESP_OK;' in n
assert 'clients.num>0' in n and '!s_manual_connect' in n
assert 's_suspend_sta_start=true' in n and 's_suspend_sta_start=false' in n
assert '#define STA_RETRY_MAX_MS 120000U' in n
for field in ('ap_running','ap_disconnect_count','ap_last_disconnect_reason','sta_connecting'):
 assert field in h
for field in ('running','disconnect_count','last_disconnect_reason','connecting'):
 assert field in w and field in a
assert 'FORGET STA' in a and 'AP-only' in a
print('CS012C WIFI PASS: AP-only, guarded STA retries and SoftAP disconnect telemetry')
