# CS008 — Appliance Diagnostics and Support Report

Scope: reusable administration, health and troubleshooting. Camera snapshot, streaming, LCD preview and gesture navigation remain DISABLED.

## Added
- GET /api/v1/diagnostics/check — read-only report about existing cached hardware states. No new camera frame, I2C re-probe, storage mutation or flash write.
- GET /api/v1/diagnostics/report — downloadable JSON support report with firmware/build, reset reason, uptime, chip/flash, minimum/free/largest-block heap, PSRAM, AP/STA/mDNS availability, FAT/WL and microSD capacity/errors, SPI/I2C/display, touch/IMU inventory and OV5640 ID/probe, auth status and 64 most recent in-memory platform events.
- Report deliberately excludes Wi-Fi SSIDs, MAC addresses, IPs and all Admin/Wi-Fi passwords. It is still an operational diagnostic log and should only be shared with trusted maintainers.
- Admin → DIAGNOSTICS: RUN READ-ONLY CHECK and DOWNLOAD SUPPORT REPORT.
- Fixed platform logs response to use heap allocation instead of a ~10 KiB stack array in the HTTP server. Diagnostics event snapshot also uses heap.
- HTTP URI capacity raised from 36 to 48 for modular services.
- Security controls from CS007 remain available.

## Safety
- Report/read-only check does not format or write flash/SD; does not request a camera frame.
- Support report is subject to Admin authentication when configured. In the default unconfigured prototype, operational APIs including the report remain publicly accessible to clients on the device's network. Configure WPA2 and Admin password before untrusted networking.
- HTTP Basic is not TLS. Do not expose Admin to Internet.
- CI compilation/packaging is not a substitute for hardware validation.

## Hardware acceptance
1. Check landing page, Admin, AP/STA, mDNS, file commander and volumes.
2. Run Diagnostics → RUN READ-ONLY CHECK and inspect PASS/WARNING/ERROR and booleans. Missing optional SD should not be treated as an automatic fatal crash.
3. Download report and ensure valid JSON, no credentials, consistent chip/memory/flash and the expected camera inventory without captures.
4. Download logs with 64 events and check HTTP stability and heap minimum.
5. Test Admin authentication and protected report access (CS007).
6. Perform several cold boots; compare minimum heap before and after filesystem operations.
7. Keep camera capture, stream, preview and OTA updater disabled.

Previous stable CI commit: CS006 9c70ef2ae4662fe8a8a9b21d499203c4083f4556. CS007 isolated green commit c91218eb811a46d187357678be8d639f6ea11fc2.
