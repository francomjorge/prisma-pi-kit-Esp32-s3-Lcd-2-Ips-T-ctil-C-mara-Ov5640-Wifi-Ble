# CS006 — Reusable Appliance Administration Platform

Camera capture remains disabled. CS006 consolidates infrastructure intended to be reusable by other PRISMA appliances.

## Added

- One shared onboard I2C service on GPIO48 SDA / GPIO47 SCL.
- CST816D hardware probe migrated to shared I2C ownership.
- QMI8658 hardware inventory, probing addresses 0x6B then 0x6A and reading WHO_AM_I (0x00) plus revision (0x01).
- No continuous IMU sampling yet.
- Platform health API: `/api/v1/health`.
- 64-entry in-memory circular platform event log: `/api/v1/logs`.
- Configuration JSON export:
  - `/api/v1/config/export` redacts passwords by default.
  - `/api/v1/config/export?include_secrets=1` includes configured network secrets explicitly.
- Configuration JSON import: `/api/v1/config/import`.
- Settings-only factory reset: `/api/v1/config/factory-reset`.
  - Internal files and SD files are preserved.
- Admin front-end split into reusable `admin.html`, `admin.css`, and `admin.js`.
- Admin Sensors section reports real QMI8658 hardware identity.
- Diagnostics uses the health and event-log APIs.
- System page exposes backup/import/reset operations.

## Still intentionally disabled

- camera capture / snapshots;
- camera stream;
- LCD camera preview;
- touch navigation;
- continuous IMU sampling;
- battery ADC telemetry until the exact BAT_ADC GPIO for this exact board revision is verified;
- Admin authentication;
- state-changing API authentication;
- ESP-NOW;
- BLE control plane;
- OTA updater.

## Hardware policy

The board documentation confirms an onboard QMI8658 IMU and 3.7 V lithium battery charge/discharge interface. Battery telemetry remains disabled because the administration platform does not guess GPIO mappings when the exact mapping has not been verified for this model/revision.

## Security note

Configuration backups exclude Wi-Fi passwords by default. A backup containing secrets requires the explicit `include_secrets=1` request. Production Admin/API authentication remains a separate milestone and is not represented as implemented in CS006.
