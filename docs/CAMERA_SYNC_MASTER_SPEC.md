# PRISMA-PI CAMERA / CAMERA SYNC — MASTER SPECIFICATION

Status: architecture baseline before new firmware line
Product family: PRISMA-PI
Product: PRISMA-PI CAMERA
Operational surface: CAMERA SYNC
Manufacturer/brand: Vudum Developments

## 1. Product contract

PRISMA-PI CAMERA is a standalone ESP32-S3 camera/video appliance. The product must remain usable without Internet, external router or cloud service. Browser access extends the appliance; it must not become a second independent source of state.

Three first-class web surfaces are mandatory:

- `/` — product presentation / landing page.
- `/camera-sync` — operational camera/video application.
- `/admin` — appliance administration.

All three surfaces use one shared visual system and one authoritative runtime state.

## 2. Product identity

Canonical strings:

- Product family: `PRISMA-PI`
- Product name: `PRISMA-PI CAMERA`
- Operational application: `CAMERA SYNC`
- Vendor: `Vudum Developments`
- Default hostname: `prisma-pi-camera`
- Default mDNS name: `prisma-pi-camera.local`
- Default AP prefix: `PRISMA-PI-CAMERA-`
- Default HTTP port: `80`

Identity must be centralized in one module/configuration source. UI, API, logs, Wi-Fi, mDNS and firmware packaging must not contain independent conflicting product names.

## 3. Visual identity

The appliance screen and all three web surfaces belong to the same product family.

Required visual language:

- white primary background;
- black/dark neutral typography;
- PRISMA `-P` multicolor identity preserved consistently;
- generous whitespace;
- restrained technical UI;
- same typography scale, spacing logic and header treatment across `/`, `/admin` and `/camera-sync`;
- no generic dark-dashboard aesthetic;
- no independent color scheme per route.

A shared web style/component layer is mandatory.

## 4. Local appliance UI

### Initial release

One static product screen only.

Content:

- PRISMA-P identity;
- CAMERA;
- CAMERA SYNC;
- simple readiness/status indication.

Requirements:

- visually match the PRISMA family / Simple Sync language;
- no touch navigation required;
- no camera preview required;
- no FPS, PID, IP, build number or debug telemetry on the normal product screen;
- no continuous animation/redraw after presentation unless explicitly required later;
- technical information belongs to `/admin`.

### Later local UI

Additional local screens are allowed only after browser/network/camera foundations are stable. Touch must not become a dependency for diagnostics or recovery.

## 5. Web route contract

### `/` — Product presentation

Purpose: institutional/product landing page, not an administration dashboard.

Must contain:

- PRISMA-P CAMERA identity;
- CAMERA SYNC product description;
- `Open Camera Sync` link to `/camera-sync`;
- `Administration` link to `/admin`;
- Vudum Developments attribution;
- configurable vendor website link;
- basic device online/offline indication only if useful;
- version/footer metadata kept secondary.

No hardware configuration controls belong here.

### `/camera-sync` — Operational product application

Purpose: operate camera/video functionality.

Top-level functional areas:

1. LIVE
2. CAMERA
3. VIDEO
4. STREAM
5. RECORD
6. PROCESS
7. OVERLAYS
8. PRESETS
9. OUTPUT

#### LIVE

- live image;
- source status;
- current resolution;
- actual FPS;
- REC state;
- stream state;
- snapshot;
- fullscreen;
- active preset/profile;
- health/error summary relevant to operation.

#### CAMERA

Camera controls, when supported by the validated OV5640 driver/sensor path:

- sensor state;
- resolution/frame size;
- JPEG quality;
- brightness;
- contrast;
- saturation;
- sharpness;
- exposure / auto exposure;
- gain / auto gain;
- white balance / auto white balance;
- mirror;
- vertical flip;
- rotation/orientation policy;
- test pattern where supported;
- reset camera controls to defaults.

Unsupported controls must be explicitly marked unsupported, never simulated.

#### VIDEO

- source format;
- source resolution;
- output resolution;
- target FPS;
- actual FPS;
- frame age/latency;
- crop;
- scale;
- orientation;
- local preview mode;
- browser output mode;
- pipeline status;
- dropped/skipped frame counters.

#### STREAM

- enable/disable;
- stream URL;
- transport/format;
- resolution;
- target/actual FPS;
- approximate bitrate/bandwidth;
- connected viewers;
- frames sent;
- dropped frames;
- capture time;
- send time;
- latency/frame age where measurable;
- errors/reconnect state;
- snapshot URL;
- start-on-boot policy later.

Initial stream policy: browser/network stream has priority over local LCD preview.

#### RECORD

Planned:

- start/stop recording;
- snapshot stills;
- clips;
- loop recording;
- duration;
- filename/session naming;
- storage destination;
- source/device/program recording model if later required;
- recording library;
- delete/download metadata.

Recording remains disabled until storage hardware/path is validated.

#### PROCESS

Planned processing engine:

- 8-bit/posterize;
- pixelation;
- glitch;
- chroma key;
- color transforms;
- crop/scale;
- face-region transforms;
- future modular effects.

Each effect must declare whether it executes on device, in browser, or on an external PRISMA node.

#### OVERLAYS

Planned:

- text;
- image overlays;
- masks;
- transparency/alpha;
- position;
- scale;
- face boxes/metadata overlays;
- layer order.

#### PRESETS

- save;
- load;
- default preset;
- rename/delete;
- camera preset;
- stream preset;
- processing preset;
- import/export JSON;
- versioned schema.

#### OUTPUT

Planned outputs:

- local LCD;
- browser/live view;
- network stream;
- recording;
- future external display/output only after hardware validation.

### `/admin` — Appliance administration

Purpose: administer the device, not operate creative camera functions.

Required sections:

1. OVERVIEW
2. DEVICE
3. NETWORK
4. STORAGE
5. DISPLAY
6. TOUCH
7. CAMERA HW
8. SENSORS
9. POWER
10. CONNECTIVITY
11. SECURITY
12. SYSTEM
13. DIAGNOSTICS
14. UPDATE
15. ABOUT

Every applicable section follows:

`STATUS -> LIVE DATA -> CONFIGURATION -> TEST -> ERRORS`

#### OVERVIEW

- product/friendly name;
- firmware version/build;
- uptime;
- overall health;
- flash/PSRAM/heap;
- network summary;
- camera summary;
- display/touch summary;
- storage summary when available;
- active errors/warnings.

#### DEVICE

- ESP32 model;
- cores/revision;
- CPU frequency where safely available;
- flash size;
- PSRAM size;
- free/minimum heap;
- reset reason;
- unique device/node ID;
- hardware/board revision when defined.

#### NETWORK

See Network specification below.

#### STORAGE

Conditional on validated hardware/storage path:

- availability;
- mount/unmount;
- filesystem;
- total/used/free;
- read/write health;
- files/directories;
- metadata;
- supported preview;
- delete/download;
- protected format operation.

#### DISPLAY

- detected/ready state;
- controller/resolution;
- orientation;
- brightness;
- backlight;
- timeout/sleep policy;
- test pattern;
- errors.

#### TOUCH

- detected/controller state;
- raw X/Y and pressed state;
- polling/interrupt mode;
- calibration/coordinate mapping where needed;
- gesture counters;
- diagnostic test;
- errors.

Touch refinement is intentionally postponed until camera/network architecture is stable.

#### CAMERA HW

Hardware/driver diagnostics only:

- OV5640 detection;
- PID/MID/version when available;
- pin/bus configuration identifier;
- XCLK;
- frame buffer mode/count/location;
- PSRAM dependency/state;
- capture self-test;
- driver errors;
- camera reset/reinitialize diagnostic action.

Creative/photographic controls belong to `/camera-sync`.

#### SENSORS

Capability-driven. Show only validated sensors as active; unvalidated planned sensors remain disabled/greyed.

#### POWER

- USB/power state that hardware can actually expose;
- backlight power policy;
- reboot/shutdown semantics;
- battery/charging/voltage only when physically measurable;
- never fabricate battery percentage.

#### CONNECTIVITY

- Wi-Fi;
- mDNS/discovery;
- ESP-NOW;
- BLE;
- future PRISMA node links.

#### SECURITY

Initial deliberate defaults:

- AP: OPEN / no password;
- Admin authentication: OFF / no password.

Must later support:

- set/change admin password;
- session/authentication policy;
- AP security mode/password;
- credential recovery/factory reset policy;
- ESP-NOW encryption/pairing policy;
- BLE pairing/access policy where needed.

#### SYSTEM

- uptime;
- time/timezone/NTP state when network time is implemented;
- reboot;
- factory reset;
- settings backup/export;
- settings restore/import;
- configuration schema version;
- system errors.

#### DIAGNOSTICS

- subsystem health matrix;
- logs;
- reset reason;
- watchdog/reset counters where available;
- heap/PSRAM minima;
- network statistics;
- camera capture/stream metrics;
- storage errors;
- self-test actions;
- downloadable diagnostic report later.

#### UPDATE

- current firmware;
- build/hash;
- OTA update later;
- update validation;
- rollback support only if partition architecture implements it;
- factory image/recovery instructions.

#### ABOUT

- product;
- vendor;
- hardware model;
- firmware version;
- licenses/third-party components;
- Vudum Developments website link.

## 6. Authoritative state architecture

There must be one runtime truth shared by:

- local appliance UI;
- `/`;
- `/admin`;
- `/camera-sync`;
- REST/realtime API;
- ESP-NOW;
- BLE;
- future PRISMA CORE integration.

Conceptual capability contract:

```text
CAPABILITY
  identity
  available
  enabled
  state
  metrics
  configuration
  commands
  events
  errors
```

UI code must not own hardware state.

## 7. Firmware architecture

Target conceptual structure:

```text
main/
  main.c                  composition/boot only

components/
  prisma_bsp/
    display/
    touch/
    camera/
    storage/
    power/
    buttons/
    usb/

  prisma_system/
    identity/
    settings/
    state/
    diagnostics/
    logging/
    factory_reset/
    update/

  prisma_camera/
    capture/
    controls/
    frames/
    snapshot/

  prisma_video/
    pipeline/
    scaling/
    processing/
    overlays/
    recording/

  prisma_network/
    wifi/
    access_point/
    mdns/
    discovery/
    espnow/
    ble/

  prisma_web/
    server/
    api/
    presentation/
    admin/
    camera_sync/
    realtime/

  prisma_storage/
    filesystem/
    presets/
    recordings/
    logs/

storage/web/
  shared/
  presentation/
  admin/
  camera-sync/
```

Board-specific GPIO/controller details belong only to the BSP layer.

## 8. Network specification

### 8.1 Factory/default mode

- SoftAP enabled.
- AP is open / no password.
- deterministic local administration IP, initially `192.168.4.1` unless later architecture requires a different subnet.
- HTTP available locally without Internet.
- device remains usable with no router.

Default AP SSID:

`PRISMA-PI-CAMERA-XXXXXX`

where suffix is derived from stable device identity/MAC.

### 8.2 Friendly name / hostname

Default hostname:

`prisma-pi-camera`

Default mDNS URL:

`http://prisma-pi-camera.local/`

Friendly name must later be configurable and persisted. Hostname validation/sanitization is required.

### 8.3 Wi-Fi STA

Later required:

- enable/disable STA;
- scan;
- SSID/RSSI/security list;
- connect;
- disconnect;
- reconnect;
- save credentials;
- forget network;
- IP/gateway/DNS/RSSI;
- connection loss detection;
- automatic recovery;
- AP+STA coexistence where stable;
- configurable fallback AP policy.

### 8.4 mDNS / local discovery

Advertise at minimum:

- hostname;
- HTTP service;
- product/node type;
- firmware version if practical;
- CAMERA SYNC capability indication.

### 8.5 ESP-NOW

ESP-NOW is a required future PRISMA transport. It is not used for continuous raw video.

Functions:

- enable/disable;
- discovery broadcast;
- node identity;
- friendly name;
- node type;
- firmware version;
- capability exchange;
- peer list;
- pair/unpair;
- unicast/broadcast commands;
- events;
- camera state/control commands where appropriate;
- effect/generation node coordination later;
- heartbeat/presence;
- disappearance/reappearance detection;
- RSSI/diagnostics where available;
- acknowledgement/retry policy for commands;
- encryption/security policy later.

Wi-Fi and ESP-NOW share the 2.4 GHz Wi-Fi radio. Channel/coexistence must be treated as an architectural constraint and tested.

### 8.6 BLE

Required future capability because ESP32-S3 provides BLE:

- enable/disable;
- advertising;
- product/node discovery;
- configuration/control service;
- connection state;
- diagnostics/RSSI where available;
- coexistence testing with Wi-Fi/ESP-NOW.

Bluetooth Classic/A2DP must not be promised on ESP32-S3.

## 9. API contract

Version APIs from the beginning.

Base:

`/api/v1`

Planned read endpoints:

- `/api/v1/status`
- `/api/v1/identity`
- `/api/v1/device`
- `/api/v1/network`
- `/api/v1/camera`
- `/api/v1/video`
- `/api/v1/stream`
- `/api/v1/storage`
- `/api/v1/connectivity`
- `/api/v1/system`
- `/api/v1/diagnostics`
- `/api/v1/capabilities`

Planned command/config endpoints are added only with the subsystem that implements them.

Requirements:

- JSON schema is deliberate and versioned;
- unsupported values are explicit;
- no fake success responses;
- control writes return applied/current state;
- web pages consume the same API/services, not private duplicate state.

Realtime telemetry transport (SSE/WebSocket) is chosen later from measured requirements.

## 10. Camera / frame architecture

The camera must have one authoritative capture service.

Rules:

- do not let LCD preview, web stream and recording independently fight for `esp_camera_fb_get()`;
- capture ownership/producer architecture is defined before simultaneous consumers are enabled;
- frame metadata includes sequence, capture timestamp, size, format and dimensions;
- stream is a consumer;
- local preview is a lower-priority consumer;
- recording is a consumer;
- processing may consume or produce frames depending on mode;
- large buffers prefer PSRAM; latency-critical/DMA structures use suitable internal memory;
- buffer counts and memory use are reported in diagnostics.

Performance priority:

1. stable capture;
2. stable web stream;
3. admin/API responsiveness;
4. local preview;
5. touch/animation extras.

## 11. Persistence / settings

NVS (or a defined persistent settings layer) must support, as features arrive:

- friendly name/hostname;
- AP settings;
- STA credentials;
- admin/security settings;
- camera controls;
- stream defaults;
- display/backlight settings;
- selected preset;
- ESP-NOW peer/security configuration;
- BLE configuration where appropriate;
- feature enable flags;
- configuration schema version.

Settings migration/defaulting rules are mandatory when schema changes.

## 12. Factory reset

Factory reset restores configuration, not user media unless separately requested.

Expected reset scope:

- Wi-Fi STA credentials;
- AP configuration;
- hostname/friendly name;
- admin credentials;
- camera settings;
- stream settings;
- selected preset/default configuration;
- ESP-NOW peers/settings;
- BLE settings;
- other persistent configurable options.

Must not silently erase recordings/media/storage content.

## 13. Storage model

Only activated if hardware/path is validated.

Suggested logical directories:

```text
/images
/overlays
/presets
/recordings
/snapshots
/logs
/system
```

Storage service must be reusable by Web Admin and Camera Sync.

## 14. Diagnostics / observability

Every subsystem exposes:

- state;
- last error;
- cumulative error counter when useful;
- activity metrics;
- memory/buffer impact where relevant;
- self-test when safe.

Global diagnostics should include:

- uptime;
- reset reason;
- free/minimum heap;
- PSRAM total/free/minimum if available;
- flash size;
- camera captures/failures;
- stream frames/drops/errors;
- network clients/reconnects;
- HTTP errors;
- ESP-NOW peer/packet stats later;
- BLE state later;
- storage state/errors later.

## 15. Security evolution

Phase 1 deliberately prioritizes bring-up:

- open AP;
- no Admin password;
- local-only assumption.

Before production deployment, security phase must add:

- admin authentication;
- session handling;
- configurable WPA2/WPA3 policy as supported;
- credential storage policy;
- input validation/CSRF considerations for state-changing web actions;
- ESP-NOW peer/encryption policy;
- BLE access policy;
- safe OTA/update validation.

## 16. Hardware capability classification

### Confirmed/known project foundations

- ESP32-S3;
- ST7789-class 240x320 LCD path;
- CST816-family capacitive touch path;
- OV5640 camera;
- Wi-Fi 2.4 GHz;
- BLE;
- flash/PSRAM configuration used by current firmware line.

### Planned but must be hardware-validated before enabling

- microSD/TF;
- battery/charging telemetry;
- IMU/sensors not already electrically confirmed on this board;
- external display/video output;
- USB functions beyond validated programming/debug role;
- any GPS or other external sensor/module.

Admin should show these as `NOT DETECTED`, `NOT SUPPORTED` or `PLANNED`, never as active without validation.

## 17. Build/release principles

- Every build implements a demonstrable functional milestone.
- Do not combine unrelated subsystems in one experimental build.
- Stable builds are tagged only after hardware acceptance.
- CI success is not physical validation.
- Every milestone records source commit, CI result, binary SHA-256 and physical test status.
- Previous physically stable milestone remains available for rollback.

## 18. Non-negotiable product rules

1. One product identity.
2. One authoritative state.
3. `/`, `/admin`, `/camera-sync` remain distinct responsibilities.
4. Local-first operation.
5. Network stream gets priority over LCD preview.
6. No unsupported hardware/feature is presented as working.
7. No touch refinements while core camera/network architecture is unstable.
8. No simultaneous camera consumers until capture ownership is designed.
9. No new feature without diagnostics/state visibility.
10. No stable label before physical validation.
