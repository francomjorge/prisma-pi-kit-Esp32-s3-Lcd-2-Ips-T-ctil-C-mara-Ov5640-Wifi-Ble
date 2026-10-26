# PRISMA PI — ESP32-S3 Touch LCD 2

Appliance independiente basado en Waveshare ESP32-S3-Touch-LCD-2. Modelo y resolución 240×320 confirmados visualmente en la placa del usuario. Cámara OV5640 validada físicamente. Build 004 confirmó preview local real; Build 005 refinó rendimiento/touch; Build 006 agrega fuente VGA y primer stream MJPEG por Wi‑Fi.

## Estado

- Build 001: base de firmware, LCD/touch, glitch, Wi‑Fi AP y administración local. CI compilado correctamente; ver [Build 001](docs/BUILD_001.md).
- Build 002: identificación e inicialización de cámara, sensor/PID y diagnóstico web/LCD; ver [Build 002](docs/BUILD_002.md).
- Build 003: primera adquisición real de un frame JPEG; ver [Build 003](docs/BUILD_003.md).
- Build 004: preview local QVGA bajo demanda. Validado físicamente: OV5640, preview real y ~17 FPS reportados por el pipeline; ver [Build 004](docs/BUILD_004.md).
- Build 005: optimización de preview y gestos: doble framebuffer con PSRAM, `GRAB_LATEST`, optimización de compilador, DMA LCD canalizado y debounce táctil; ver [Build 005](docs/BUILD_005.md).
- Build 006: cámara en JPEG VGA 640×480 quality 10, preview local VGA→320×240, stream MJPEG en puerto 81 y filtrado de taps fantasma por confirmación/liberación estable; ver [Build 006](docs/BUILD_006.md).

## Base funcional

Tomar como referencia funcional PRISMA-PI-LITE únicamente a nivel conceptual. Este appliance mantiene drivers, configuración y builds propios. No modificar otros repositorios PRISMA desde este proyecto.

## Diagnóstico del appliance

- Detectar procesador, Flash, PSRAM y heap.
- Verificar LCD, touch, navegación y estabilidad de gestos.
- Identificar OV5640 y validar captura JPEG.
- Medir captura, decode, salida LCD y FPS reales.
- Medir stream: clientes, frames, bytes y FPS.
- Mantener administración funcional aun si cámara/stream falla.
- Probar microSD, IMU, batería, Wi‑Fi STA, mDNS y ESP‑NOW en etapas posteriores.

## Video

- Fuente Build 006: VGA 640×480 JPEG.
- Preview local: decode 1/2 a 320×240 y rotación lógica de LCD.
- Stream web: MJPEG directo en `http://<ip>:81/stream`.
- Administración: puerto 80.
- Un cliente de stream simultáneo en la primera implementación.

## Diseño de comunicaciones

ESP-NOW transportará comandos/eventos/estado. Wi‑Fi/IP transporta administración, imágenes, archivos y streaming. El stream usa servidor HTTP separado para no bloquear la interfaz administrativa.

## Etapas

1. Revisar esquema/ejemplos oficiales y documentar pines.
2. Fijar versiones reproducibles: ESP-IDF 5.3.2 y `esp32-camera` 2.0.15.
3. Validar cámara: identidad → frame → preview local → optimización → stream IP.
4. Integrar configuración persistente y administración completa.
5. Validar Wi‑Fi STA, mDNS y ESP‑NOW.
6. Medir streaming simultáneo con preview local, FPS, PSRAM y estabilidad.
7. Incorporar exposición/color, overlays, efectos, grabación y loops en builds separados.

OTA, BLE/MIDI y detección de rostros quedan como ampliaciones posteriores. No se presupone compatibilidad directa con ESP-WHO hasta validarla en esta placa.

## Fuentes oficiales

- [Placa Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2)
- [Recursos oficiales](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2/Resources-And-Documents)
- [Esquema eléctrico](https://files.waveshare.com/wiki/ESP32-S3-LCD-2/ESP32-S3-Touch-LCD-2-SchDoc.pdf)
- [ESP-IDF Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2/ESP-IDF)
- [ESP32-S3 datasheet](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf)
- [esp32-camera](https://github.com/espressif/esp32-camera)

Las referencias web pueden cambiar; la integración fija versiones concretas donde corresponde.
