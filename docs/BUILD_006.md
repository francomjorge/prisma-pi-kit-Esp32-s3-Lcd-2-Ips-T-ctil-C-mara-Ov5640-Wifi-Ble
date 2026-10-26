# Build 006 — VGA + Web Stream

## Objetivo

Avanzar desde el preview local físicamente validado hacia el primer streaming MJPEG por Wi‑Fi, mientras se corrige el tap residual/espurio observado en Build 005 y se mejora la calidad de imagen de origen.

## Cámara y calidad

- Sensor esperado: OV5640.
- Fuente: JPEG VGA 640×480.
- JPEG quality: 10 (menor número = mayor calidad en sensores OV según esp32-camera).
- Con PSRAM: `fb_count=2` y `CAMERA_GRAB_LATEST`.
- Preview local: el JPEG VGA se decodifica a 320×240 con `JPG_SCALE_2X`, suficiente para la LCD física 240×320 rotada.
- Stream web: entrega el JPEG VGA directo, sin volver a codificar.

## Streaming

- Administración: puerto 80.
- MJPEG: puerto 81, endpoint `/stream`.
- Se usa un servidor HTTP separado para que una conexión MJPEG larga no bloquee `/admin` ni `/api/status`.
- Un cliente de streaming simultáneo en esta primera etapa.
- Buffer de stream: 384 KiB, preferentemente en PSRAM.
- Métricas: clientes, frames, bytes, tiempo de ciclo y FPS aproximados.

## Touch / taps fantasma

La placa oficial usa controlador capacitivo CST816D por I2C. Build 006 mantiene polling pero agrega filtrado temporal:

- Poll: 10 ms.
- Un contacto debe existir durante 3 muestras consecutivas para considerarse válido.
- La liberación también requiere 3 muestras consecutivas.
- Pulsos cortos descartados se contabilizan como `touch_rejected`.
- Tap válido: mínimo 30 ms, máximo 500 ms y movimiento máximo 18 px.
- Swipe: 36 px horizontales mínimos y predominio horizontal.
- Debounce post-swipe: 220 ms.
- Al entrar al preview local, el firmware NO acepta un tap de salida hasta detectar una liberación completa y luego un contacto nuevo. Esto evita que el mismo dedo que entra al preview provoque una salida inmediata.

## API de diagnóstico

Se agregan o mantienen:

- `touch_confirm_samples`
- `touch_rejected`
- `stream_server`
- `stream_port`
- `stream_clients`
- `stream_frames`
- `stream_bytes`
- `stream_frame_ms`
- `stream_fps`
- `stream_error`
- métricas de preview local
- resolución y calidad JPEG de cámara

## Administración web

En `VIDEO & STREAM`:

- botón **Iniciar stream**
- botón **Detener stream**
- visor MJPEG VGA
- métricas de stream y preview local

## Prueba física de aceptación

1. Confirmar portada `BUILD 006`.
2. Hacer 10–20 swipes y verificar que no haya navegación múltiple por un único gesto.
3. Entrar y salir del preview local 10 veces; el mismo toque de entrada no debe generar salida automática.
4. Dejar el preview local 2 minutos.
5. Abrir `/admin` desde un cliente Wi‑Fi y arrancar el stream desde `VIDEO & STREAM`.
6. Confirmar imagen VGA real en navegador.
7. Confirmar que `/api/status` sigue respondiendo con el stream abierto.
8. Probar local preview + stream simultáneos y observar caída de FPS, heap mínimo y estabilidad.
9. Si hay resets, corrupción de imagen o bloqueo de admin, no avanzar a múltiples clientes.

## Fuera de alcance

- múltiples viewers simultáneos
- grabación
- exposición/color configurables desde UI
- detección de rostro
- overlays y efectos
- mDNS
