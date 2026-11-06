# CS009 — Local OTA, boot recovery and rollback

Camera Synth remains in infrastructure-only mode: no snapshot, stream, LCD camera preview, camera effects or continuous IMU sampling.

## Partition compatibility

Existing 16 MiB layout is unchanged:
- ota_0: offset 0x20000, size 0x400000 (4 MiB)
- ota_1: offset 0x420000, size 0x400000 (4 MiB)
- data FAT/WL: 0x820000, size 0x7E0000
- otadata: 0xF000, size 0x2000

CS009 enables CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y in sdkconfig.defaults. The **bootloader is NOT updated by an application-only OTA**. To guarantee the new bootloader supports rollback, first install CS009 by USB with the appropriate full bootloader + partition table + app flashing procedure (after backing up settings and INTERNAL files). A CS008 bootloader updated only via OTA may not implement automatic rollback.

## API / Admin

- GET /api/v1/update/status — running and next partitions, running version, rollback compile flag, boot state and whether a previous valid OTA image exists.
- POST /api/v1/update/upload — raw ESP32-S3 **application BIN** (not 0x0 merged factory BIN), streaming without buffering the full image in RAM.
- POST /api/v1/update/rollback — request a manual return to the previous verified OTA partition, only if one is available.

Write API is guarded by CS007 Admin Auth and X-PRISMA-Action: 1. An OTA is refused when Admin Auth is disabled or AP configured OPEN. Upload additionally requires X-PRISMA-OTA: app and X-PRISMA-SHA256: <64 hex characters>; manual rollback requires X-PRISMA-Confirm: rollback.

The main Admin -> UPDATE page provides file selection, trusted expected SHA-256 input, confirmation checkbox, upload action and rollback button conditional on status.

## Upload / boot protections

1. Require Admin Auth active and WPA2 AP setting; browser must be on trusted local network.
2. Reject payload larger than target OTA partition or with invalid size/headers.
3. Verify ESP image magic and esp_app_desc magic; only project_name prisma_camera_synth accepted. Reject identical firmware version to avoid accidental reflash.
4. Receive in 4096-byte chunks and calculate SHA-256 while writing the inactive OTA slot.
5. Confirm the expected SHA-256 **before** calling esp_ota_set_boot_partition. esp_ota_end validates ESP image layout/checksum before switching slot.
6. After successful response, reboot. The first boot of new firmware is confirmed only after Admin HTTP + network + display startup. When configured bootloader rollback is active and core self-test fails, firmware requests rollback; bootloader also reverts after a reset during pending verification.
7. Manual rollback only if esp_ota_check_rollback_is_possible reports previous valid image; no raw flash erase.

## Security caveats

- SHA-256 of an expected file protects transfer integrity if the checksum comes from a trusted independent source. **It does not authenticate the firmware publisher**; HTTP Basic without TLS remains unsuitable for untrusted networks.
- Secure Boot, signed OTA, flash encryption, HTTPS and rate limiting are NOT implemented. This is local prototyping OTA, not production secure OTA.
- AP WPA2 is a precondition but does not make STA access safe from eavesdropping by other network participants. Keep STA disconnected when transmitting sensitive OTA credentials on untrusted networks.
- NVS settings and INTERNAL/SD are preserved by OTA app updates. Flashing a merged 0x0 factory image or 16 MB full image may overwrite settings, partitions and INTERNAL data.
- Do not power off during an update. A failed/incomplete write leaves running slot unchanged.

## Build artifact change

Workflow packages two different binaries:
- PRISMA-PI-CAMERA-SYNTH-CS009-factory.bin: merged 0x0 serial image, includes bootloader and partitions.
- prisma_camera_synth.bin: APP-only image appropriate for the OTA upload API.
- SHA256SUMS.txt: SHA-256 of both files.

## Physical acceptance pending

1. Boot CS009 USB factory image on target; verify admin, AP WPA2, Files/SD, mDNS, health and OV5640 inventory only.
2. Enable Admin Auth and WPA2; check /api/v1/update/status reports running slot and size.
3. Reject factory BIN uploaded through OTA and wrong product app / bad SHA-256 / oversized upload.
4. OTA-update to a future higher-version Camera Synth app and confirm boot slot switch and CS009+ app self-test.
5. Confirm incomplete/invalid OTA fails without changing current boot partition.
6. Test rollback only after a valid prior OTA image is available.
7. Test loss of power during a future controlled OTA with physical recovery available; do not assume automatic rollback works without bootstrap bootloader.
8. Verify NVS/INTERNAL/SD file persistence after OTA, not after factory flashing.
9. Check heap minimum, upload RAM use, AP reconnection and whether HTTP Basic authentication remains active.
