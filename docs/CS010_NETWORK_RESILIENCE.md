# CS010 — Unique mDNS Identity & Network Resilience

## Device identity
- Factory-default hostname on each unit is generated once from stable Wi-Fi SoftAP MAC address: `prisma-pi-camera-synth-xxxxxx` (lowercase six hexadecimal digits).
- Example `http://prisma-pi-camera-synth-a1b2c3.local/` is illustrative; actual suffix comes from hardware. The base name alone is no longer shared by all devices.
- NVS persists the hostname. Existing CS009 settings with exact generic default `prisma-pi-camera-synth` are migrated to the per-device hostname at CS010 startup. A custom user hostname is **not overwritten**.
- The AP SSID already has a unique MAC suffix and stays independent of mDNS hostname.
- mDNS publishes the same hostname through `discovery.c`; Admin DEVICE/NETWORK show the address and provide a clickable `.local` link.
- DHCP/STA IP addresses may change and are no longer the primary interface address. If client device or network blocks multicast DNS, use the still-visible IP as a recovery fallback. mDNS is link-local and cannot be guaranteed across different routed subnets.
- The administrator can choose a custom hostname; uniqueness of custom names is not centrally guaranteed.

## Network resilience
- STA disconnects do not trigger a busy retry loop. Retry timer: 2 s, 4 s, 8 s, 16 s, then 30 s cap.
- Retry counter resets on STA IP acquisition or manual retry; the recovery SoftAP is never disabled by STA failures.
- Admin exposes configured/connected status, connection attempts, disconnect count, last disconnection reason code and next retry delay.
- Admin NETWORK adds a manual RETRY CONNECTION control and operational POST `/api/v1/network/retry` under existing Admin Auth + CSRF header guard.
- Network/identity/AP settings are validated and committed to NVS in **one** transaction. Changes to AP, SSID, password and mDNS hostname still take effect after reboot; STA actions are immediate.
- Running AP status is read from Wi-Fi driver, not pending settings; on AP+STA, effective AP channel can follow STA network channel.
- Camera remains hardware-inventory-only: no JPEG, stream or preview.

## Physical acceptance (not established by CI)
1. Flash/boot CS010 and confirm the unique hostname shown in Admin DEVICE and NETWORK agrees with `GET /api/v1/status` `hostname` and `mdns_url`.
2. On the same Wi-Fi/mDNS-capable device, open that exact `.local` address. Confirm graceful IP fallback if client resolution is unavailable.
3. Reboot twice; unique name remains identical.
4. With CS009 generic NVS name, verify one-time migration; with a custom hostname, verify preservation.
5. Connect STA to a network then power off the router: observe retry interval and reason code, and verify the appliance recovery AP still accepts connections.
6. Restart the router, confirm STA reconnects and backoff resets. Use RETRY CONNECTION and FORGET STA.
7. Edit AP SSID and hostname together; verify atomic persistence and activation after reboot.
8. Verify Admin Auth, FILES/SD, diagnostics, OTA, touch and IMU inventory continue to operate.

## Limitations
A MAC suffix provides stable device-level differentiation, not a universal collision-proof name service. mDNS support depends on the client and LAN multicast. There is still no HTTPS and no production-grade OTA signing. Physical verification has not yet happened.
