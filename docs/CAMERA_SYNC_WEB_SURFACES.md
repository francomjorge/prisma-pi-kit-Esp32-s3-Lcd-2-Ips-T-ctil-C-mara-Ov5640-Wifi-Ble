# PRISMA-PI CAMERA — WEB SURFACES SPECIFICATION

This document defines the permanent responsibility and visual relationship of `/`, `/camera-sync` and `/admin`.

## 1. Shared shell

All pages share:

- PRISMA-P CAMERA branding;
- white background;
- same typography family and type scale;
- same spacing grid;
- same PRISMA `-P` multicolor identity;
- same footer treatment;
- same product/vendor naming;
- same responsive breakpoints;
- no external CDN dependency;
- no duplicated product constants.

Shared top navigation:

```text
PRODUCT      CAMERA SYNC      ADMIN
   /          /camera-sync    /admin
```

The active section is visually indicated without changing the overall product identity.

Shared footer:

```text
PRISMA-PI CAMERA
A Vudum Developments product
[vendor website]
firmware/build metadata (secondary)
```

Vendor URL is a configurable/canonical product constant and must not be hard-coded inconsistently across pages.

## 2. `/` — Product presentation

### Purpose

The root route is a product landing page. It should look like a web extension of the physical appliance screen, not like an engineering dashboard.

### Required content

Hero:

- PRISMA-P
- CAMERA
- CAMERA SYNC
- short product description

Primary actions:

- `OPEN CAMERA SYNC` -> `/camera-sync`
- `ADMINISTRATION` -> `/admin`

Institutional content:

- Vudum Developments attribution;
- vendor site link;
- product family context;
- optional concise device online/status indicator.

### Allowed technical information

Only secondary, unobtrusive information:

- online/offline;
- firmware version in footer/about area;
- device friendly name if useful.

### Forbidden on `/`

- camera controls;
- Wi-Fi forms;
- memory/heap tables;
- stream tuning;
- sensor diagnostics;
- touch settings;
- storage browser;
- factory reset;
- logs.

## 3. `/camera-sync` — Operational application

### Purpose

Camera Sync is where a user operates the visual/camera product.

### Primary navigation

```text
LIVE
CAMERA
VIDEO
STREAM
RECORD
PROCESS
OVERLAYS
PRESETS
OUTPUT
```

These entries exist from the product-shell milestone, but unavailable features are disabled and clearly marked `PLANNED`, `NOT CONFIGURED`, `NOT DETECTED` or `UNSUPPORTED` as appropriate.

Never display fake live values or fake success state.

### LIVE layout

Target conceptual layout:

```text
┌────────────────────────────────────────────────────────────┐
│ PRISMA-P CAMERA        CAMERA SYNC              STATUS ●   │
├────────────────────────────────────────────────────────────┤
│ LIVE CAMERA VIDEO STREAM RECORD PROCESS OVERLAYS ...       │
├────────────────────────────────────────────────────────────┤
│                                                            │
│                    PRIMARY IMAGE AREA                      │
│                                                            │
├──────────────────────────────┬─────────────────────────────┤
│ SOURCE / FPS / RESOLUTION    │ STREAM / RECORD / HEALTH    │
└──────────────────────────────┴─────────────────────────────┘
```

When no camera path exists yet, the image area displays a clean `CAMERA NOT CONFIGURED` state instead of a placeholder video.

### CAMERA

Contains user-facing photographic/sensor controls only.

Does not contain GPIO, XCLK, framebuffer allocation or low-level driver diagnostics; those belong to Admin / CAMERA HW.

### VIDEO

Contains user-facing pipeline behavior:

- source/output dimensions;
- target/actual FPS;
- crop/scale/orientation;
- local preview mode;
- frame latency/drop metrics.

### STREAM

Contains operational streaming state and controls.

Does not contain general Wi-Fi credentials or AP configuration; those belong to Admin / NETWORK.

### RECORD

Contains recording controls/library only after storage/recording service exists.

### PROCESS / OVERLAYS

Contains creative image processing. Every effect exposes its execution location when relevant: DEVICE, BROWSER or NODE.

### PRESETS

Contains reusable operational state, not global device backup.

### OUTPUT

Contains operational media outputs, not network administration.

## 4. `/admin` — Appliance administration

### Purpose

Admin manages the appliance itself.

### Navigation

```text
OVERVIEW
DEVICE
NETWORK
STORAGE
DISPLAY
TOUCH
CAMERA HW
SENSORS
POWER
CONNECTIVITY
SECURITY
SYSTEM
DIAGNOSTICS
UPDATE
ABOUT
```

### Common section structure

Where applicable:

```text
STATUS
LIVE DATA
CONFIGURATION
TEST
ERRORS
```

### OVERVIEW

Global health and important warnings only.

### DEVICE

CPU/SoC, flash, PSRAM, heap, reset reason, node/device ID and hardware identity.

### NETWORK

AP, STA, IP addressing, hostname, mDNS, clients, RSSI and recovery policy.

### STORAGE

Physical/filesystem administration. Not operational recording controls.

### DISPLAY

Hardware display/backlight administration.

### TOUCH

Raw/controller diagnostics and later gesture configuration.

### CAMERA HW

Low-level camera hardware/driver diagnostics:

- sensor identity;
- XCLK/config;
- framebuffer configuration;
- PSRAM relationship;
- capture test;
- reset/reinitialize;
- errors.

No creative camera settings here unless needed strictly for a hardware diagnostic test.

### SENSORS

Capability-driven inventory/diagnostics only.

### POWER

Power state and controls physically supported by hardware.

### CONNECTIVITY

Wi-Fi service status summary plus ESP-NOW/BLE/discovery management.

### SECURITY

Admin/AP/transport security policy.

### SYSTEM

Reboot, factory reset, config backup/restore, time and general system settings.

### DIAGNOSTICS

Logs, health matrix, counters, memory minima, errors and self-tests.

### UPDATE

Firmware update/recovery information and controls when implemented.

### ABOUT

Product/vendor/version/licenses.

## 5. Shared status vocabulary

Use consistent state labels across all pages/API:

- `READY`
- `ACTIVE`
- `INACTIVE`
- `DEGRADED`
- `ERROR`
- `NOT DETECTED`
- `NOT CONFIGURED`
- `UNSUPPORTED`
- `PLANNED`

Avoid ambiguous states such as generic `OK` where the user cannot tell whether the feature is enabled, merely detected or actively running.

## 6. Disabled/planned controls

A disabled future feature must:

- remain visually identifiable;
- explain why it is disabled;
- never issue an API call;
- never show fake telemetry;
- never imply that the hardware exists.

Examples:

- `RECORD — Storage not validated`
- `ESP-NOW — Planned for CS012`
- `BATTERY — Not detected on current hardware`

## 7. Responsive behavior

### Desktop/notebook

- persistent/top navigation as appropriate;
- camera image gets maximum useful area on Camera Sync;
- Admin can use sidebar or structured section navigation.

### Mobile

- shared top product navigation remains accessible;
- Admin navigation collapses/scrolls cleanly;
- controls remain touch-sized;
- image maintains aspect ratio;
- no horizontal page overflow for normal content.

## 8. Accessibility / usability baseline

- semantic labels;
- adequate contrast;
- visible focus states;
- buttons distinguishable from status indicators;
- state not communicated by color alone;
- destructive actions require explicit confirmation later;
- long technical IDs/URLs wrap safely.

## 9. Performance rules

Web presentation must not interfere with video pipeline.

- static assets served locally;
- avoid unnecessary high-frequency polling;
- status polling rate appropriate to metric;
- realtime channel introduced only when needed;
- Admin refresh cannot take camera ownership;
- loading `/admin` must not stop/degrade stream by design.

## 10. Product consistency rule

If another PRISMA appliance later adopts the same architecture, it should be able to reuse the same shell with only:

- product identity;
- operational route name;
- operational feature modules;
- hardware capability descriptor.

The visual family and `/` + `/admin` responsibility remain consistent.
