# PRISMA-PI CAMERA / CAMERA SYNC — FEATURE MATRIX

Legend:

- `MUST` — product requirement.
- `LATER` — required in a later milestone, not initial bring-up.
- `HW` — conditional on hardware validation.
- `OPTIONAL` — useful extension, not product baseline.

This matrix is the checklist used to decide whether a feature has been forgotten.

| Domain | Feature | Priority | Surface | Persistence | Diagnostics |
|---|---|---:|---|---|---|
| Identity | Canonical PRISMA-PI CAMERA product name | MUST | all | firmware | yes |
| Identity | Vudum Developments attribution/link | MUST | `/`, About | config/firmware | n/a |
| Identity | Friendly device name | LATER | Admin | yes | yes |
| Identity | Stable node/device ID | MUST | Admin/API/discovery | derived/stable | yes |
| Local UI | White PRISMA family landing screen | MUST | LCD | no | display health |
| Local UI | Static screen after boot | MUST | LCD | no | yes |
| Local UI | Touch-independent startup/recovery | MUST | LCD/system | no | yes |
| Local UI | Additional camera/control pages | LATER | LCD | yes where relevant | yes |
| Web | `/` product presentation | MUST | Web | no | HTTP health |
| Web | `/admin` administration | MUST | Web | settings | HTTP/API health |
| Web | `/camera-sync` operational UI | MUST | Web | presets/settings | HTTP/API health |
| Web | Shared style/components | MUST | all Web | bundled | n/a |
| Web | Responsive phone/notebook layout | MUST | all Web | no | n/a |
| Web | No external CDN dependency | MUST | all Web | bundled | n/a |
| API | `/api/v1/status` | MUST | API | no | self |
| API | `/api/v1/capabilities` | MUST | API | no | self |
| API | Versioned JSON contract | MUST | API | schema version | yes |
| API | Realtime SSE/WebSocket | LATER | Web/API | no | yes |
| System | Uptime | MUST | Admin/API | no | yes |
| System | Reset reason | MUST | Admin/API | no | yes |
| System | Heap free/minimum | MUST | Admin/API | no | yes |
| System | PSRAM state/metrics | MUST | Admin/API | no | yes |
| System | Flash size/state | MUST | Admin/API | no | yes |
| System | Reboot command | LATER | Admin/API | no | yes |
| System | Factory reset | LATER | Admin/API | n/a | yes |
| System | Config export/import | LATER | Admin | file | yes |
| System | NTP/time/timezone | LATER | Admin/API | yes | yes |
| Update | Firmware/build/hash visibility | MUST | Admin/About | firmware | yes |
| Update | OTA | LATER | Admin | update state | yes |
| Update | Rollback | LATER | Admin | partition-dependent | yes |
| Network | SoftAP enabled factory default | MUST | system/Admin | yes later | yes |
| Network | Open AP / no password initially | MUST | system/Admin | yes later | yes |
| Network | Default `192.168.4.1` local access | MUST | network | config | yes |
| Network | SSID `PRISMA-PI-CAMERA-XXXXXX` | MUST | network/Admin | derived/config | yes |
| Network | Configurable AP SSID | LATER | Admin | yes | yes |
| Network | WPA2/WPA3 policy/password | LATER | Admin | yes | yes |
| Network | Wi-Fi STA scan/connect | LATER | Admin | yes | yes |
| Network | STA reconnect | LATER | service | yes | yes |
| Network | Forget saved network | LATER | Admin | yes | yes |
| Network | AP+STA coexistence | LATER | service | yes | yes |
| Network | Fallback AP | LATER | service | yes | yes |
| Network | Configurable hostname | LATER | Admin | yes | yes |
| Network | `prisma-pi-camera.local` mDNS | MUST | network | hostname | yes |
| Network | mDNS HTTP service advertisement | MUST | network | no | yes |
| Discovery | Product/node capability advertisement | LATER | network | no | yes |
| ESP-NOW | Enable/disable | LATER | Admin/API | yes | yes |
| ESP-NOW | Discovery broadcast | LATER | connectivity | no | yes |
| ESP-NOW | Pair/unpair peers | LATER | Admin/API | yes | yes |
| ESP-NOW | Capability exchange | LATER | connectivity | no | yes |
| ESP-NOW | Bidirectional commands/events | LATER | connectivity | no | yes |
| ESP-NOW | Heartbeat/presence | LATER | connectivity | no | yes |
| ESP-NOW | Command ACK/retry | LATER | connectivity | no | yes |
| ESP-NOW | Encryption/security policy | LATER | Admin | yes | yes |
| ESP-NOW | Continuous raw video transport | OPTIONAL/NO BASELINE | n/a | n/a | n/a |
| BLE | Advertising/discovery | LATER | connectivity | yes | yes |
| BLE | Configuration/control service | LATER | connectivity | yes | yes |
| BLE | Wi-Fi coexistence validation | MUST before BLE release | diagnostics | no | yes |
| Camera HW | OV5640 probe | MUST | Admin/API | no | yes |
| Camera HW | Sensor identity PID/MID/version | MUST | Admin/API | no | yes |
| Camera HW | XCLK/config diagnostics | MUST | Admin/API | firmware | yes |
| Camera HW | Framebuffer count/location | MUST | Admin/API | config | yes |
| Camera HW | Capture self-test | MUST | Admin/API | no | yes |
| Camera | Single authoritative capture producer | MUST | service | no | yes |
| Camera | Snapshot JPEG | MUST before stream | Camera Sync/API | no | yes |
| Camera | Resolution control | LATER | Camera Sync | yes | yes |
| Camera | JPEG quality | LATER | Camera Sync | yes | yes |
| Camera | Exposure controls | LATER | Camera Sync | yes | yes |
| Camera | Gain controls | LATER | Camera Sync | yes | yes |
| Camera | White balance | LATER | Camera Sync | yes | yes |
| Camera | Brightness/contrast/saturation | LATER | Camera Sync | yes | yes |
| Camera | Sharpness/denoise if supported | LATER | Camera Sync | yes | yes |
| Camera | Mirror/vflip/rotation | LATER | Camera Sync | yes | yes |
| Video | Frame sequence/timestamp metadata | MUST | pipeline/API | no | yes |
| Video | Frame age/latency | MUST when streaming | Camera Sync/Admin | no | yes |
| Video | Source/output dimensions | MUST | Camera Sync/API | yes | yes |
| Video | Target/actual FPS | MUST | Camera Sync/API | yes | yes |
| Video | Dropped/skipped frame counters | MUST | Camera Sync/Admin | no | yes |
| Video | Crop/scale | LATER | Camera Sync | yes | yes |
| Stream | Browser stream | MUST | Camera Sync | yes | yes |
| Stream | Network stream priority over LCD preview | MUST | service | yes/policy | yes |
| Stream | Start/stop | MUST | Camera Sync/API | yes | yes |
| Stream | Viewer count | MUST | Camera Sync/Admin | no | yes |
| Stream | Capture/send timing | MUST | Camera Sync/Admin | no | yes |
| Stream | Bitrate/bandwidth estimate | LATER | Camera Sync | no | yes |
| Stream | Reconnect/error state | MUST | Camera Sync | no | yes |
| Stream | Multiple viewers | LATER | Camera Sync | no | yes |
| LCD Preview | Local camera preview | LATER | LCD/Camera Sync | yes | yes |
| LCD Preview | OFF/ECO/FULL mode | LATER | Camera Sync/Admin | yes | yes |
| LCD Preview | Lower priority than web stream | MUST | service | yes | yes |
| Recording | Snapshot storage | HW | Camera Sync | yes | yes |
| Recording | Video/clip recording | HW | Camera Sync | yes | yes |
| Recording | Loop recording | HW | Camera Sync | yes | yes |
| Recording | Recording library | HW | Camera Sync | storage | yes |
| Processing | 8-bit/posterize | LATER | Camera Sync | presets | yes |
| Processing | Pixel/glitch/color effects | LATER | Camera Sync | presets | yes |
| Processing | Chroma key | LATER | Camera Sync | presets | yes |
| Processing | Device/browser/node execution declaration | MUST for each effect | Camera Sync | preset metadata | yes |
| Overlay | Text overlay | LATER | Camera Sync | presets | yes |
| Overlay | Image/mask overlay | LATER | Camera Sync | presets/storage | yes |
| Overlay | Alpha/position/scale | LATER | Camera Sync | presets | yes |
| Overlay | Face boxes/metadata | LATER | Camera Sync | presets | yes |
| Presets | Save/load | LATER | Camera Sync | yes | yes |
| Presets | Import/export JSON | LATER | Camera Sync | file | yes |
| Presets | Versioned preset schema | MUST before presets release | service | yes | yes |
| Display | ST7789 init/status | MUST | LCD/Admin | firmware | yes |
| Display | Orientation | LATER | Admin | yes | yes |
| Display | Brightness/backlight | LATER | Admin | yes | yes |
| Display | Test pattern | LATER | Admin | no | yes |
| Touch | CST816 probe/status | MUST as hardware inventory | Admin | no | yes |
| Touch | Raw X/Y diagnostic | LATER | Admin | no | yes |
| Touch | Tap/swipe navigation | LATER | LCD | config | yes |
| Touch | Interrupt mode if validated | LATER | BSP | firmware/config | yes |
| Storage | microSD/TF support | HW | Admin/Camera Sync | n/a | yes |
| Storage | Capacity/free/files | HW | Admin | no | yes |
| Storage | Safe format | HW | Admin | no | yes |
| Storage | `/images` | HW | storage | n/a | yes |
| Storage | `/overlays` | HW | storage | n/a | yes |
| Storage | `/presets` | HW | storage | n/a | yes |
| Storage | `/recordings` | HW | storage | n/a | yes |
| Storage | `/snapshots` | HW | storage | n/a | yes |
| Storage | `/logs` | HW | storage | n/a | yes |
| Power | USB power status if measurable | HW | Admin | no | yes |
| Power | Battery voltage/charge if measurable | HW | Admin | no | yes |
| Power | Controlled shutdown if supported | HW | Admin/local | no | yes |
| Sensors | IMU/other onboard sensors | HW | Admin/API | yes | yes |
| Sensors | GPS/external sensors | OPTIONAL/HW | Admin/API | yes | yes |
| USB | Programming/debug | MUST | system | no | yes |
| USB | Serial device role | HW/LATER | Admin | yes | yes |
| USB | MIDI/HID/etc. | OPTIONAL/HW | Admin | yes | yes |
| Security | Admin authentication OFF initially | MUST initial policy | Admin | yes later | yes |
| Security | Admin password | LATER | Admin | yes | yes |
| Security | Sessions | LATER | Web | yes | yes |
| Security | Input validation | MUST | API/Web | no | yes |
| Diagnostics | Subsystem health matrix | MUST | Admin/API | no | self |
| Diagnostics | Last error + counters | MUST | Admin/API | no | self |
| Diagnostics | Self-test actions | LATER | Admin | no | self |
| Diagnostics | Downloadable diagnostic report | LATER | Admin | file | self |

## Release rule

A row is not marked implemented because a menu item exists. It is implemented only when:

1. service behavior exists;
2. API/state reflects it;
3. errors are exposed;
4. physical/device test has passed where hardware is involved;
5. regression against previous stable milestone passes.
