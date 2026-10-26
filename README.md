# PRISMA-PI CAMERA — ESP32-S3 Touch LCD 2 + OV5640

Appliance independiente basado en Waveshare ESP32-S3-Touch-LCD-2 con cámara OV5640. Modelo y resolución LCD 240×320 confirmados físicamente. El nombre de producto queda estandarizado como **PRISMA-PI CAMERA**.

## Estado

- Build 001: base de firmware, LCD/touch, glitch, Wi‑Fi AP y administración local.
- Build 002: identificación e inicialización de cámara, sensor/PID y diagnóstico web/LCD.
- Build 003: primera adquisición real de un frame JPEG.
- Build 004: preview local QVGA bajo demanda. Validado físicamente: OV5640, preview real y ~17 FPS reportados por el pipeline.
- Build 005: optimización de preview y gestos: doble framebuffer con PSRAM, `GRAB_LATEST`, DMA LCD y debounce táctil.
- Build 006: fuente JPEG VGA 640×480 quality 10 + primer stream MJPEG en puerto 81. Validación física: el stream web funciona fluido y con buena calidad mientras el preview local está cerrado; preview local simultáneo degrada el rendimiento y el filtro táctil de confirmación triple empeora tap/swipe.
- Build 007: build de estabilización. Mantiene VGA/stream, restaura touch directo tipo Build 005, reduce umbral de swipe, separa lock de cámara del decode local, limita adaptativamente el FPS del LCD cuando existe stream web, inicia AP abierto y aplica rotación de sensor 180°. Pendiente de validación física.

## Base funcional

Tomar como referencia funcional PRISMA-PI-LITE únicamente a nivel conceptual. Este appliance mantiene drivers, configuración y builds propios. No modificar otros repositorios PRISMA desde este proyecto.

## Diagnóstico del appliance

- Detectar procesador, Flash, PSRAM y heap.
- Verificar LCD, touch, navegación y estabilidad de gestos.
- Identificar OV5640 y validar captura JPEG.
- Medir captura, decode, salida LCD y FPS reales.
- Medir stream: clientes, frames, bytes y FPS.
- Mantener administración funcional aun si cámara/stream falla.
- Mantener `/api/status` como fuente completa de estado del firmware.
- Probar microSD, IMU, batería, Wi‑Fi STA, mDNS y ESP‑NOW en etapas posteriores.

## Video

- Fuente Build 007: VGA 640×480 JPEG quality 10.
- Preview local: decode 1/2 a 320×240.
- Stream web: MJPEG directo en `http://<ip>:81/stream`.
- Administración: puerto 80.
- Un cliente de stream simultáneo en la implementación actual.
- El preview local libera el framebuffer antes de decodificar para reducir contención con el stream.
- Política temporal adaptativa: LCD ~12 FPS objetivo sin stream y ~4 FPS objetivo con stream activo.
- Rotación inicial del sensor: 180° mediante `vflip + hmirror`.

## Red

- SSID: `PRISMA-PI-CAMERA-XXXXXX`.
- Build 007 inicia en AP abierto, sin contraseña, para simplificar las pruebas.
- Seguridad WPA2/WPA3 configurable queda como siguiente paso, sin eliminar la posibilidad de AP abierto.

## Diseño de comunicaciones

ESP-NOW transportará comandos/eventos/estado. Wi‑Fi/IP transporta administración, imágenes, archivos y streaming. El stream usa servidor HTTP separado para no bloquear la interfaz administrativa.

## Etapas

1. Validar Build 007: touch, navegación, stream solo y stream + preview local.
2. Medir FPS, tiempos de captura/decode/display y heap con ambas rutas de video.
3. Si la estrategia es estable, compartir frames entre stream y LCD para evitar capturas duplicadas.
4. Agregar rotación 0°/180° seleccionable y persistente.
5. Agregar configuración de seguridad AP y Wi‑Fi STA.
6. Validar mDNS y ESP‑NOW.
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
