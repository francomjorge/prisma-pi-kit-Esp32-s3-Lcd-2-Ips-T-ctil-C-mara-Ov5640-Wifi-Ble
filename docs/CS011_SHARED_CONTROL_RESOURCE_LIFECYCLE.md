# CS011+ — Shared runtime state, bidirectional controls, resource lifecycle
Status: implementation contract — first adopted after physically stable CS010Y #68
Hardware baseline: ESP32-S3 + OV5640, 16 MiB flash. Preserve the existing PRISMA SYSTEM hidden gate, black LCD, admin recovery and CS010Y rollback.

## Non-negotiable principle
The ESP32 device owns the single authoritative runtime state. Both LCD and Web show the same state and send commands to the same subsystem functions. Neither surface may invent independent "on"/"off" flags, synthetic FPS, fake images or demo logs. Every accepted command returns a result and a new state revision. A rejected command must include an error and must not fake success.

## State/control schema (proposed prisma.runtime.v1)
Common for each subsystem:
- `supported`: derived from real hardware/validated implementation.
- `desired`: OFF / READY / ACTIVE according to user request.
- `actual`: OFF / STARTING / READY / CAPTURING / STREAMING / STOPPING / ERROR, based on real runtime.
- `owner`: NONE / LCD / WEB / SYSTEM / API; provenance of most recent accepted command, not exclusive ownership.
- `revision`: monotonically increasing on state transitions.
- `last_error`, `last_change_uptime_ms`.
- `memory_bytes`, `task_active`, `buffers_allocated` when measurable; never estimate as zero when unknown.

State updates: Web reads a status endpoint on page load and after each command, with bounded polling while visible. Later SSE may replace polling. LCD must query/read the very same state struct after commands. Volatile runtime state defaults OFF on cold boot except essential networking and Admin. Settings saved to NVS must be opt-in and explicitly identified.

## CS011: manual snapshot and controlled camera lifecycle
- Camera driver is one producer; one serialized owner controls esp_camera_fb_get / esp_camera_fb_return.
- CAMERA OFF: no active capture, no stream, no frame retention; deinit driver and release its PSRAM framebuffer allocations when safely possible.
- CAMERA READY: sensor initialized, buffers allocated, zero ongoing capture task.
- TAKE PHOTO: explicit command, capture exactly one JPEG, include timestamp/sequence, actual dimensions, encoded bytes, capture latency; always return frame to the driver, even on transmission error.
- Camera READY may be toggled from either UI. If OFF, snapshot must reject clearly or support an explicit one-shot auto-open/auto-close mode (not implicitly leave resources allocated).
- Conflict: while capture is in progress, return BUSY; no concurrent independent fb_get loops.
- Failure: error state and metrics; do not crash UI/HTTP/ADMIN.
- Controls from LCD must appear within configuration/operational pages, not as an extra main PRISMA carousel page unless separately approved.
- CS011 does not implement continuous video, recording, live LCD camera preview, effects or an independent camera task.

## Logs/diagnostics as a runtime service
Modes: OFF / ERROR / INFO / DEBUG. Event severity cutoff applies to application event ring buffer, HTTP diagnostics and future capture debug events; emergency boot/panic/watchdog diagnostics remain possible, distinctly described.
- Default INFO for troubleshooting, user can set OFF to avoid normal event collection.
- OFF must cease new ordinary event retention and optional filesystem logging; cannot claim to silence unavoidable ESP-IDF/ROM messages without separate console log controls.
- ERROR must record ERROR events only; INFO includes ERROR, WARN and INFO; DEBUG includes all.
- Reading log entries must NOT implicitly enable collection or start camera.
- Optional `clear logs` requires explicit action and affects only volatile log ring.
- Web and LCD display same current level. No high-frequency console printing inside frame loops.
- Stream-specific frame tracing remains OFF by default, even at DEBUG, until deliberate enabling because of CPU budget.

## Resource switch matrix
| Feature | OFF must mean | ON/active means | Avoid |
| --- | --- | --- | --- |
| Camera driver | deinit and free driver framebuffers once consumers stop | initialized, idle until capture command | implicit continuous capture |
| Snapshot | no retained frame | one requested JPEG with fb returned | background capture |
| Web Stream (CS012) | worker stopped, socket closed, no frame backlog | bounded worker and viewer count | unlimited queues |
| LCD preview (later) | no decoder/preview task/buffer | rate-limited consumer of shared producer | second independent fb_get |
| Recording (later) | file closed/flushed and writer stopped | writer owns bounded queue | capturing forever after disconnect |
| Processing/overlays (later) | bypass, free owned scratch buffers | budgeted per-frame processing | effect kept running with no outputs |
| Application logs | event/filter collection stopped, not kernel panic handling | selected severity and capped ring | log every frame |
| Browser viewer | no device capture merely for showing a disabled page | image/stream only on explicit activation | auto-start by opening Admin |

## Explicit API contract for CS011 candidate
GET /api/v1/runtime: state revision, camera/log modes, capture metrics, memory.
POST /api/v1/camera/enable: start camera only on explicit request (authorized when configured).
POST /api/v1/camera/disable: cancel/reject if busy; after completion deinit/free resources.
POST /api/v1/camera/snapshot: explicit on-demand JPEG, CSRF-protected; reject if camera OFF; no cache, no implicit stream.
GET /api/v1/logs/config: current application log mode and dropped event counters.
POST /api/v1/logs/config: chosen OFF/ERROR/INFO/DEBUG level.
GET /api/v1/logs: read-only; never enables anything.
Any command returns post-change actual state. Use no browser-local shadow state; control must be available through the shared runtime and, as LCD pages gain relevant controls, call those same functions.

## Web / LCD mapping
PRISMA home: preserve exact established animation/glitch.
SYSTEM hidden gate: preserve CONFIG / BACK TO PRISMA.
Current device configuration pages (DEVICE / NETWORK / STORAGE / SECURITY / SYSTEM) stay in place, including reset confirmations.
Camera operations live on the operational /camera-synth page with manually triggered screenshot and resource switch. An LCD camera control should be added as a coherent, explicit internal operational item only after user approves the LCD workflow; it must call the same runtime functions.
Logs are owned by Admin diagnostics; choose mode and read events, with visible actual mode. The LCD view of the mode must be read from the same service; no simulated indicator.

## CS011B implementation candidate (not physically validated)
- LCD PRISMA main carousel remains unchanged, as does the hidden PRISMA SYSTEM gate.
- The internal CONFIG set expands to six pages, appending CAMERA after SYSTEM; the existing five pages and Back to Prisma controls remain.
- LCD CAMERA: CAMERA ON, TAKE PHOTO (writes an internal `/snapshots/shot_XXXXXXXXXX.jpg` JPEG), CAMERA OFF.
- Web `/camera-synth` LIVE: CAMERA ON, TAKE PHOTO (returns a JPEG only after explicit click), CAMERA OFF; local preview blob is revoked on OFF / leaving LIVE.
- Admin FILES can browse/download the LCD-generated stills; runtime capture metrics and camera enabled state are device-owned.
- LCD SYSTEM has a logs level cycle; Web ADMIN DIAGNOSTICS has the same OFF/ERROR/INFO/DEBUG runtime control.
- STOP means the camera driver calls `esp_camera_deinit` and releases its driver-owned framebuffer(s). The core HTTP/Admin service and boot/panic safeguards remain available.
- No continuous stream, video recording, postprocessing or camera auto-start exists in this candidate.
- This implementation is a candidate requiring physical verification on the target board. CS010Y #68 remains the known working fallback.

## Acceptance gates
1. Preserve CS010Y stable boot, Wi-Fi AP, mDNS, Admin, LCD gestures and factory recovery.
2. 100 manual snapshot requests including error/retry; no unreturned framebuffers.
3. Camera OFF returns resources (measured PSRAM free before/after); camera READY does not capture continuously.
4. Logs OFF/ERROR/INFO/DEBUG honored across Web and LCD; Admin remains available when logging OFF.
5. Every user command reports actual status to both interfaces, including failure and BUSY.
6. No automatic streaming/recording after opening the browser; no watchers/tasks left on disable.
7. Record heap/PSRAM minimum, capture times, reset counters and CI build evidence.
8. Physical validation required before calling CS011 stable or moving to CS012.

Never renumber historical CS010Y: this file is the normalized contract for forward work, superseding the original speculative CS004-CS010 camera milestones.
