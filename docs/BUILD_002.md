# Build 002.0 — identificación de cámara

Objetivo: avanzar G0 sin habilitar todavía preview, streaming, grabación ni procesamiento. El firmware inicializa el driver de cámara con el pinout extraído de los ejemplos oficiales de Waveshare y reporta la identidad real del sensor.

## Dependencia reproducible

- ESP-IDF: 5.3.2, igual que Build 001.
- espressif/esp32-camera: 2.0.15 exacto mediante `main/idf_component.yml`.
- Formato inicial: JPEG, QVGA 320×240, un framebuffer. No se solicita un frame en este build.
- Framebuffer en PSRAM cuando existe; fallback a DRAM solo para permitir diagnóstico si no hay PSRAM.

## Pinout de cámara usado

- D0..D7: 12, 13, 15, 11, 14, 10, 7, 2.
- XCLK / PCLK: 8 / 9.
- VSYNC / HREF: 6 / 4.
- SCCB SDA / SCL: 21 / 16.
- PWDN / RESET: 17 / -1.
- XCLK: 20 MHz, LEDC timer 0 / channel 0. El backlight sigue siendo GPIO fijo en este build, por lo que no comparte LEDC.

## Diagnóstico nuevo

`/api/status` incorpora:

- `camera`: probing / ready / error.
- `camera_sensor`: nombre informado por el driver.
- `camera_pid`: PID hexadecimal.
- `camera_expected`: true solo si PID == OV5640.
- MIDH, MIDL y VER del sensor.
- `camera_error`: `none` o error real de inicialización.

La página CAMERA del administrador muestra estos datos. La segunda pantalla local pasa a CAMERA DIAGNOSTIC y muestra estado, sensor, PID y confirmación OV5640. La portada conserva el glitch y ahora indica BUILD 002.

## Aislamiento de fallos

La red y el servidor de administración arrancan antes del probe. La pantalla se inicializa de forma independiente. Si la cámara no responde, el firmware registra el error y continúa operativo: no se borra NVS, no se deshabilita la administración y no se presenta una función falsa como disponible.

## Fuera de alcance

- Obtener frame o mostrar preview.
- Streaming MJPEG.
- Controles de exposición/color/flip.
- SD, IMU, batería, mDNS y ESP-NOW.
- Efectos, overlays, detección de rostros y grabación.

## Prueba física requerida

1. Flashear el binario fusionado en 0x0 y confirmar que la portada, touch, glitch y Wi-Fi del Build 001 no retrocedieron.
2. Ir a CAMERA DIAGNOSTIC: con el kit esperado debe indicar CAMERA READY, sensor OV5640 y PID 5640.
3. Abrir `/admin` y comprobar CAMERA: estado ready, PID 0x5640, `OV5640 esperado = Sí`, `camera_error = none`.
4. Comparar heap libre/mínimo antes y después del probe y dejar el equipo 30 minutos sin resets.
5. Si el sensor no coincide o falla, guardar el error exacto y no avanzar a captura hasta resolver G0.

Compilar valida integración de software, no identidad física. El siguiente build debe depender de esta prueba antes de habilitar captura/preview.
