# PRISMA-PI CAMERA / CAMERA SYNC — IMPLEMENTATION ROADMAP

This roadmap replaces the previous experimental Build 006/007/008 progression for future development. Those builds remain historical references only.

Naming convention for the new line:

`CS001`, `CS002`, ...

Each milestone must be physically tested before being called stable.

## CS001 — Product shell

### Goal
Establish the product identity and three-route architecture with minimum runtime complexity.

### Appliance
- initialize ESP32-S3 platform;
- initialize display/backlight;
- show one static white PRISMA-P CAMERA / CAMERA SYNC product screen;
- no camera initialization;
- no touch navigation;
- no local preview;
- no continuous animation/redraw.

### Network
- SoftAP only;
- open AP / no password;
- generated SSID `PRISMA-PI-CAMERA-XXXXXX`;
- local IP `192.168.4.1`;
- HTTP server.

### Web
- `/` product presentation;
- `/admin` complete administration shell with inactive/planned sections;
- `/camera-sync` complete operational shell with inactive/planned sections;
- shared visual system;
- `/api/v1/status` basic system/network/display state;
- `/api/v1/capabilities` reports only capabilities actually present/validated at this milestone.

### Exit criteria
- 20 cold boots without unexplained reset;
- LCD presentation remains static/stable;
- phone and notebook can join open AP;
- `/`, `/admin`, `/camera-sync`, `/api/v1/status` repeatedly load;
- no memory leak from repeated page refresh;
- no camera/touch dependency.

---

## CS002 — Identity, settings and local naming

### Goal
Make the device consistently addressable and establish persistent configuration architecture.

### Functions
- central product identity module;
- stable node/device ID;
- friendly name;
- hostname default `prisma-pi-camera`;
- mDNS `prisma-pi-camera.local`;
- HTTP service advertisement;
- persistent settings/schema version;
- Admin Overview/Device/Network become live;
- reset reason, flash, PSRAM, heap/uptime diagnostics.

### Exit criteria
- IP access and `.local` access both work where client mDNS supports it;
- reboot preserves settings;
- invalid hostname input is rejected/sanitized;
- factory-default identity is deterministic.

---

## CS003 — Hardware inventory only

### Goal
Probe hardware without starting camera streaming/preview.

### Functions
- display inventory/health;
- CST816-family touch probe/status only;
- OV5640 init/probe/identity only;
- camera hardware diagnostics;
- flash/PSRAM inventory;
- conditional probe for any storage/sensors actually present on PCB;
- capability descriptor becomes authoritative.

### Exit criteria
- repeated probe stable;
- camera sensor identity reported correctly;
- absence/failure of secondary hardware does not crash Web Admin;
- no continuous camera capture.

---

## CS004 — Single capture / snapshot

### Goal
Validate one controlled camera capture path before streaming.

### Functions
- one authoritative camera capture service;
- JPEG capture;
- frame sequence/timestamp/metadata;
- snapshot endpoint/view;
- capture latency/size/errors;
- no LCD preview;
- no continuous stream.

### Exit criteria
- repeated snapshots are valid;
- no framebuffer leaks;
- no camera lockup after repeated requests;
- memory minima remain stable.

---

## CS005 — Web stream only

### Goal
Build the first continuous video path with no competing LCD camera consumer.

### Functions
- browser stream;
- start/stop;
- actual FPS;
- capture/send timing;
- frame/drop counters;
- viewer state;
- stream error/reconnect telemetry;
- Camera Sync LIVE becomes operational;
- LCD remains on product/status UI, not camera preview.

### Exit criteria
- sustained stream is visually fluid and stable;
- 30-minute stream without reset/leak;
- Admin remains responsive while streaming;
- stream stop/restart repeated successfully.

---

## CS006 — Stream stabilization / performance release

### Goal
Optimize only the web stream before adding another video consumer.

### Functions/tests
- measured capture producer cadence;
- buffer strategy in PSRAM;
- frame age/latency;
- target vs actual FPS;
- resolution/quality combinations;
- Wi-Fi bandwidth impact;
- browser reconnect;
- multiple viewer architecture decision;
- performance profile defaults.

### Exit criteria
- selected production stream profile documented;
- 1-hour stream burn-in;
- stable heap/PSRAM minima;
- zero watchdog/reset;
- reproducible FPS/latency baseline.

---

## CS007 — OV5640 controls

### Goal
Make Camera Sync CAMERA page functional without altering stream architecture.

### Functions
- resolution/profile control;
- JPEG quality;
- exposure/AEC where driver supports;
- gain/AGC;
- AWB/white balance;
- brightness;
- contrast;
- saturation;
- sharpness/other validated controls;
- mirror/vflip/orientation;
- defaults/reset;
- settings persistence.

### Exit criteria
- every exposed control demonstrably affects output;
- unsupported controls are omitted/marked unsupported;
- settings survive reboot when persistence is enabled;
- stream remains stable.

---

## CS008 — Video pipeline model

### Goal
Formalize video source/output profiles before LCD preview or processing.

### Functions
- source vs output resolution;
- frame metadata service;
- target/actual FPS;
- crop/scale architecture;
- stream profile;
- output consumer registration;
- frame producer/consumer ownership documented and measured.

### Exit criteria
- one camera producer feeds consumers conceptually/architecturally;
- no independent competing `fb_get` loops are introduced.

---

## CS009 — Local LCD preview

### Goal
Add LCD video as a secondary consumer.

### Policy
Web stream has priority.

### Functions
- local preview OFF/ECO/FULL modes;
- LCD consumer rate independently limited;
- no camera ownership during expensive local decode/scale;
- local FPS and display timing diagnostics.

### Exit criteria
- stream-alone baseline remains essentially unchanged;
- enabling LCD may lower LCD FPS, but must not make network stream unusable;
- preview can be disabled completely.

---

## CS010 — Touch/navigation

### Goal
Only now reintroduce touch as a user-interface feature.

### Functions
- raw diagnostics first;
- coordinate mapping;
- tap;
- swipe only after tap/release behavior is stable;
- interrupt-driven mode evaluated if hardware wiring supports it;
- debounce state machine documented;
- touch can never starve stream/camera tasks.

### Exit criteria
- gesture regression test passes;
- no phantom navigation in defined test period;
- stream performance unaffected within accepted tolerance.

---

## CS011 — Wi-Fi STA / AP policy

### Goal
Complete normal network administration.

### Functions
- scan;
- STA connect/disconnect;
- saved credentials;
- reconnect;
- forget;
- AP+STA;
- fallback AP;
- configurable AP SSID/security/password;
- configurable hostname/mDNS;
- client/network diagnostics.

### Exit criteria
- reboot/reconnect works;
- loss/recovery works;
- factory AP remains a recovery path;
- mDNS works in supported LAN environment.

---

## CS012 — ESP-NOW / PRISMA discovery

### Goal
Connect CAMERA into the PRISMA node ecosystem.

### Functions
- ESP-NOW enable/status;
- node identity/type/name/version/capabilities;
- discovery;
- peer list;
- pair/unpair;
- presence/heartbeat;
- bidirectional commands/events;
- suitable camera/control metadata;
- ACK/retry policy;
- diagnostics;
- security/encryption policy foundation.

### Constraint
No continuous raw video requirement over ESP-NOW.

### Exit criteria
- two PRISMA nodes discover each other;
- disappearance/reappearance detected;
- bidirectional control/state exchange reliable;
- Wi-Fi/ESP-NOW channel coexistence tested.

---

## CS013 — BLE

### Goal
Expose PRISMA BLE discovery/config/control without destabilizing Wi-Fi/video.

### Exit criteria
- advertising/discovery works;
- supported control/config path works;
- Wi-Fi stream coexistence test passes.

---

## CS014 — Storage / snapshots / files

Conditional on validated storage hardware.

### Functions
- mount/health/capacity;
- files;
- snapshot storage;
- download/delete;
- images/overlays/presets directories;
- protected format.

### Exit criteria
- sustained read/write test;
- no corruption on normal restart/use;
- stream remains stable.

---

## CS015 — Recording

Conditional on storage throughput and architecture.

### Functions
- clip recording;
- loop recording;
- recording metadata/library;
- storage pressure handling;
- source/device/program model if still required.

### Exit criteria
- defined recording duration sustained;
- safe stop/finalization;
- no stream collapse beyond documented profile limits.

---

## CS016 — Presets

### Functions
- camera/video/stream presets;
- save/load/default;
- import/export;
- versioned schema/migration.

---

## CS017 — Processing

### Functions
- first validated effects;
- explicit execution location: DEVICE / BROWSER / NODE;
- performance budget per effect;
- bypass mode.

---

## CS018 — Overlays

### Functions
- text/image/mask overlays;
- alpha/position/scale/layers;
- face metadata overlays when detection path exists.

---

## CS019 — Security

### Functions
- admin password/authentication;
- sessions;
- AP security configuration;
- secure state-changing API policy;
- ESP-NOW/BLE security policy;
- recovery behavior.

---

## CS020 — OTA / backup / recovery

### Functions
- OTA if partition architecture supports it;
- update validation;
- config backup/restore;
- safe recovery/factory image path;
- rollback only when actually implemented.

---

## Platform release acceptance

Before declaring a production/stable platform release:

- 100 cold boots;
- 100 soft reboots;
- 8-hour burn-in;
- 2-hour continuous web stream;
- repeated stream start/stop/reconnect;
- Wi-Fi loss/recovery;
- AP/STA recovery path;
- mDNS test;
- ESP-NOW coexistence test when enabled;
- BLE coexistence test when enabled;
- storage/recording tests if enabled;
- continuous Admin/API polling;
- touch navigation test if enabled;
- zero crashes/watchdog resets;
- record heap/PSRAM minima and subsystem error counters.

## Version evidence required per milestone

Every milestone record must include:

- milestone ID;
- source commit SHA;
- CI run and conclusion;
- factory binary name/size/SHA-256;
- 16 MiB image name/size/SHA-256 when produced;
- physical test result;
- known limitations;
- exact rollback target.
