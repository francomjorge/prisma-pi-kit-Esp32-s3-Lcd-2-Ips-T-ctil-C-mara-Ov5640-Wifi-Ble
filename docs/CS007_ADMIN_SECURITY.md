# CS007 — Admin Security Foundation

Status: code and CI success do not establish hardware acceptance. Camera snapshot/stream/LCD preview remain out of scope.

## Goals

1. Keep the product landing page and Camera Synth shell public.
2. Add optional persistent **Admin password** independent of Wi-Fi AP password.
3. Once configured, HTTP Basic protects /admin, its JS/CSS, and all operational /api/v1/ endpoints except public capabilities and security-state summaries. Includes Files, downloads, uploads, backups, logs, networking, reboot and settings reset.
4. Require X-PRISMA-Action: 1 header for every API POST. Admin JS adds the header to API requests.
5. Never store plaintext Admin password: PBKDF2-HMAC-SHA256, 12,000 iterations, 128-bit salt, 256-bit hash; separate NVS prisma_auth namespace.
6. Change the password by presenting current password. Username admin. New password 12–63 ASCII printable non-space characters.
7. Block configuration backup including secrets when Admin password is not configured. Normal export redacts Wi-Fi credentials.
8. Factory reset of settings clears Admin credentials; preserves internal FAT files, SD and firmware.

## Security limitations

- Initial/default Admin mode is NOT CONFIGURED to preserve CS006 compatibility. Admin and API are OPEN until explicitly enabled.
- HTTP Basic operates on HTTP, **without TLS**. Credentials travel in requests; do not expose the device to the public Internet or untrusted networks. Set WPA2 on AP before configuring Admin password.
- POST header helps defend against browser cross-origin forms, but does not replace authentication, TLS or authorization.
- Backup/import never includes the Admin password.
- No rate-limiting, roles, signed firmware, secure boot or HTTPS in CS007. These remain separate milestones.
- Invalid credential storage fails closed; recovery then requires physical NVS maintenance.
- Security is a local prototype feature, NOT a claim of production-grade security.

## API

GET /api/v1/security exposes enabled, auth state, HTTP_BASIC transport, http_tls=false, and AP open status (no secrets).
POST /api/v1/security with JSON current_password/new_password and X-PRISMA-Action: 1 sets or changes credentials. Browser then reloads to prompt for Basic credentials.

## Acceptance checklist (physical tests pending)

1. First boot: Admin page opens without password; SECURITY shows NOT CONFIGURED.
2. Normal Admin POSTs still work, but POST missing X-PRISMA-Action gets 403.
3. Set AP WPA2, reboot, then configure Admin password.
4. Protected /admin and operational APIs return 401 with incorrect Basic credentials; correct admin password works.
5. Confirm Files read/write, config backup with secrets, logs, reboot and network config are protected.
6. Change Admin password; old no longer works; new does.
7. Factory reset settings clears Admin credentials while preserving FAT/SD media.
8. Verify I2C touch/IMU probe, OV5640 hardware inventory (no capture), storage mounts, AP/STA and mDNS survive upgrade.
9. Stress reboot/reconnect and check minimum heap. Do not mark stable without physical feedback.

## Rollback

Last previously verified compile: CS006, commit 9c70ef2ae4662fe8a8a9b21d499203c4083f4556. Do not format volumes during routine upgrade.
