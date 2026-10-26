# CAMERA SYNC CS001 — IMPLEMENTATION

CS001 is the first firmware of the new Camera Sync product line. It is intentionally independent from the Build 001–008 camera/stream experiments.

## Runtime included

- ESP32-S3 platform + NVS initialization.
- ST7789 240x320 static product presentation.
- One display draw only; no UI refresh task.
- Touch not initialized.
- OV5640 not initialized and camera driver not included as a managed dependency.
- Open SoftAP, no password.
- SSID `PRISMA-PI-CAMERA-XXXXXX` from SoftAP MAC.
- Default local IP `192.168.4.1`.
- Hostname constant `prisma-pi-camera`; mDNS intentionally deferred to CS002.
- HTTP port 80.
- `/` product presentation.
- `/admin` complete permanent administration shell.
- `/camera-sync` complete operational shell.
- `/api/v1/status` real CS001 state.
- `/api/v1/capabilities` explicit available/enabled/planned states.

## Source separation

- `identity.h`: canonical product identity.
- `main.c`: composition/boot only.
- `display.c`: board display presentation only.
- `network.c`: SoftAP/network state only.
- `web.c`: HTTP routes/API only.
- `web/shared.css`: one visual system for all web surfaces.

## Explicit exclusions

No camera init, JPEG capture, MJPEG stream, local preview, touch gestures, Wi-Fi STA, mDNS, ESP-NOW, BLE, storage mount, recording, processing, overlays, presets, admin authentication, AP password or OTA.

## Hardware constants retained from validated board work

LCD ST7789 240x320, SPI2 SCLK39 MOSI38 MISO40 DC42 CS45, backlight GPIO1, 40 MHz SPI.

These are hardware facts, not inherited application architecture.

## Acceptance

Use `docs/CAMERA_SYNC_CS001_ACCEPTANCE.md`. CI success alone does not make CS001 physically stable.
