# CS005 — Storage & Files Foundation

CS005 remains an administration/infrastructure build. Camera capture, streaming and LCD preview remain disabled.

## Storage architecture

### Internal
- Existing `storage` SPIFFS partition at `0x820000`, size `0x7e0000`.
- Mounted at `/internal` when valid.
- Never silently formats after a mount failure.
- Admin can explicitly initialize/format the dedicated internal partition with confirmation.
- 12 simultaneously open files configured. This is **not** a total-file quota.

### microSD / TF
- Hardware slot is present on ESP32-S3-Touch-LCD-2 / 2-C.
- Official Waveshare schematic maps the TF card as SPI sharing SCLK/MOSI/MISO with the LCD bus:
  - SCLK GPIO39
  - MOSI GPIO38
  - MISO GPIO40
  - CS GPIO41
- LCD CS remains GPIO45.
- Card mounts at `/sdcard` using FAT via SDSPI.
- No automatic formatting.
- 12 simultaneously open files configured. This is **not** a total-file quota.
- Automatic hot-plug monitoring is deferred; mount is attempted at boot and can be retried manually.

Official hardware source:
https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2/ESP32-S3-Touch-LCD-2-SchDoc.pdf

## Shared SPI policy

SD must enter SPI mode before normal LCD traffic on the shared bus. CS005 therefore introduces one board-level SPI initializer. It drives both CS lines high, initializes SPI2 once, attempts SD mount, and only then initializes/draws the LCD.

## File browser

Admin gains a `FILES` section with a dual-pane browser:
- INTERNAL on the left;
- SD on the right;
- per-volume path navigation;
- folders first / client-side sorting;
- create folder;
- multi-file upload (one streamed request per file);
- download / inline preview;
- rename;
- delete file or empty directory;
- file copy between volumes or folders;
- refresh;
- image preview for common browser image formats;
- text preview for small text/config/log files.

The server streams directory listings and file downloads. It does not impose an arbitrary directory-entry count.

## Safety rules

- Filesystem requests are sandboxed to `/internal` or `/sdcard`.
- `..`, backslash traversal, control characters and invalid volume IDs are rejected.
- Upload is streamed to disk instead of buffering the whole file in RAM.
- Upload is rejected if it cannot fit in currently available space (existing overwritten file size is credited).
- SD and internal filesystems are never auto-formatted by normal boot.
- Recursive directory delete is deliberately not enabled.
- Directory copy is deliberately not enabled yet; file copy is supported.

## Deferred

- automatic SD hot-plug task;
- filesystem quotas by asset class;
- media index/database;
- recycle bin;
- recursive directory copy/delete;
- admin authentication/authorization;
- OTA package upload;
- camera-generated files.
