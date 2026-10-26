# Build 003.0 — first frame test

Objetivo: avanzar desde la identificación del sensor a una prueba real de adquisición sin habilitar todavía preview ni streaming continuo.

## Qué hace

- Conserva LCD, touch, glitch, Wi-Fi AP y administración local.
- Conserva la inicialización e identidad de cámara de Build 002.
- Si la cámara inicializa correctamente, solicita exactamente un frame mediante `esp_camera_fb_get()`.
- Configuración de cámara: JPEG, QVGA 320×240, calidad 12, un framebuffer.
- Registra ancho, alto, formato, tamaño en bytes y tiempo de adquisición.
- Devuelve inmediatamente el framebuffer al driver; no guarda ni transmite la imagen.
- Si la captura falla, la administración y el resto del appliance continúan funcionando y muestran el error.

## Diagnóstico nuevo

`/api/status` agrega:

- `capture`: pending / ready / error.
- `frame_width` y `frame_height`.
- `frame_bytes`.
- `frame_format`.
- `capture_ms`.
- `capture_error`.

La pantalla CAMERA + FRAME TEST muestra estado de cámara, sensor/PID y, si hay captura válida, resolución, bytes y tiempo. La página VIDEO & STREAM de administración expone las mismas métricas.

## Criterio de aprobación física

1. Build 003 arranca sin reset loops.
2. LCD, touch, glitch, Wi-Fi AP y `/admin` siguen operativos.
3. Cámara informa `ready`; en el kit esperado el PID debe ser `0x5640`.
4. `capture` debe ser `ready`.
5. El frame esperado debe reportar 320×240, formato JPEG y `frame_bytes > 0`.
6. `capture_error` debe ser `none`.
7. Registrar heap libre/mínimo y confirmar estabilidad del equipo durante al menos 30 minutos.

## Fuera de alcance

- Renderizar el JPEG en la pantalla.
- Endpoint de imagen o descarga del frame.
- MJPEG / streaming continuo.
- FPS sostenido.
- Grabación, SD y loops.
- Efectos, overlays y detección de rostro.

El siguiente build sólo debe habilitar preview después de confirmar físicamente que esta prueba entrega frames válidos de forma estable.
