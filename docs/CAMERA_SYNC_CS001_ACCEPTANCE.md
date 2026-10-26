# CAMERA SYNC CS001 — ACCEPTANCE SPECIFICATION

CS001 is the new product-shell baseline. It intentionally does not initialize the camera, stream video, navigate by touch or perform image processing.

## Required runtime behavior

### Boot

1. ESP32-S3 boots without reset loop.
2. NVS/basic platform initialization succeeds or reports a clear degraded state.
3. LCD initializes.
4. Backlight turns on.
5. One white PRISMA-P CAMERA / CAMERA SYNC product screen is drawn.
6. The screen remains static after presentation.
7. SoftAP starts.
8. HTTP server starts.

### Local display

Required:

- white visual language;
- PRISMA family branding;
- CAMERA / CAMERA SYNC identification;
- no debug telemetry;
- no build number in primary product composition;
- no touch instructions;
- no camera preview;
- no periodic full-screen redraw.

Touch may be completely uninitialized in CS001 if that produces the cleanest baseline.

### Network

Factory default:

- SoftAP ON;
- AP OPEN;
- no password;
- SSID format `PRISMA-PI-CAMERA-XXXXXX`;
- local administration address `192.168.4.1`;
- maximum client count explicitly configured;
- Wi-Fi settings held in a dedicated network service/module, not UI code.

mDNS is deferred to CS002 so CS001 remains minimal.

### `/`

Must:

- render product presentation;
- visually match appliance/family;
- show Vudum Developments attribution;
- provide link to `/camera-sync`;
- provide link to `/admin`;
- not expose configuration controls.

### `/camera-sync`

Must render the full product shell/navigation:

- LIVE
- CAMERA
- VIDEO
- STREAM
- RECORD
- PROCESS
- OVERLAYS
- PRESETS
- OUTPUT

In CS001 all operational capabilities are inactive and explicitly marked as not configured/planned.

It must not initialize or request the camera.

### `/admin`

Must render all permanent administration sections:

- OVERVIEW
- DEVICE
- NETWORK
- STORAGE
- DISPLAY
- TOUCH
- CAMERA HW
- SENSORS
- POWER
- CONNECTIVITY
- SECURITY
- SYSTEM
- DIAGNOSTICS
- UPDATE
- ABOUT

Only CS001-available state is live. Everything else is clearly disabled/planned/not detected.

### API

`GET /api/v1/status` must return valid JSON containing at minimum:

- product;
- firmware/build;
- uptime;
- display state;
- SoftAP state;
- SSID;
- AP IP;
- connected station count;
- heap free/minimum;
- PSRAM total/state;
- camera capability state reported as not initialized/not configured;
- touch capability state reported accurately;
- overall health state.

`GET /api/v1/capabilities` must distinguish:

- available;
- enabled;
- planned/unsupported/not detected.

No endpoint may report fake camera/stream success.

## Explicitly excluded from CS001

- OV5640 init;
- JPEG capture;
- stream server;
- camera preview;
- touch gestures;
- animated glitch loop;
- Wi-Fi STA;
- mDNS;
- ESP-NOW;
- BLE services;
- storage mount;
- recording;
- image processing;
- overlays;
- presets;
- admin password;
- AP password;
- OTA.

The UI may show these as future capabilities only.

## Stability tests

### Boot test

- 20 cold boots;
- 20 software reboots once reboot command exists (otherwise deferred);
- zero watchdog resets;
- zero unexpected boot loops.

### HTTP test

From phone and notebook where available:

- open `/` 20 times;
- open `/admin` 20 times;
- open `/camera-sync` 20 times;
- request `/api/v1/status` 100 times;
- request `/api/v1/capabilities` 100 times;
- navigate repeatedly between three web surfaces;
- no server lockup/reset.

### Memory test

Record:

- free heap after boot;
- minimum free heap;
- PSRAM total/free if exposed;
- same metrics after repeated HTTP tests.

No significant monotonic leak is accepted.

### Wi-Fi test

- phone joins open AP;
- notebook joins open AP;
- disconnect/reconnect client repeatedly;
- HTTP remains reachable;
- second client does not crash device.

### Display test

- correct orientation;
- correct white background/branding;
- no visible periodic refresh/flicker;
- remains unchanged while web clients are active.

## Artifact/evidence required

CS001 is not declared physically stable until recorded:

- source commit SHA;
- GitHub Actions run ID + SUCCESS;
- factory binary filename;
- factory binary byte size;
- factory SHA-256;
- 16 MiB image filename/size/SHA-256 if produced;
- physical boot result;
- AP join result;
- three-route web result;
- API result;
- memory result;
- known limitations.

## Rollback

Previous experimental binaries remain archived, but CS001 becomes the baseline for the new Camera Sync product line only after the above physical acceptance passes.
