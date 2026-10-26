# Build 005 — Preview + Touch Tuning

## Objetivo

Refinar el primer preview local validado físicamente en Build 004 antes de habilitar streaming por red.

## Cambios de rendimiento

- ESP32-S3 mantiene CPU a 240 MHz.
- Compilación cambia de optimización por tamaño a optimización por rendimiento.
- Cámara OV5640 sigue en JPEG QVGA 320×240.
- Calidad JPEG cambia de 12 a 15 para reducir carga de captura/decode sin cambiar resolución.
- Con PSRAM disponible: `fb_count=2` y `CAMERA_GRAB_LATEST`.
- Sin PSRAM: fallback a un framebuffer y `CAMERA_GRAB_WHEN_EMPTY`.
- Loop de preview elimina la espera fija de 20 ms y deja sólo 1 ms de yield.
- LCD conserva SPI a 40 MHz, pero usa dos buffers DMA y hasta dos transferencias en vuelo.
- Bloques LCD pasan a 20 líneas por transferencia.

No se sube todavía el clock SPI: primero se miden las mejoras sin forzar el panel fuera de la configuración ya validada.

## Cambios táctiles

- Poll táctil: 10 ms.
- Swipe horizontal mínimo: 36 px (Build 004: 55 px).
- El swipe debe ser predominantemente horizontal.
- Debounce post-swipe: 180 ms y liberación real del dedo antes de aceptar otro gesto.
- Tap confirmado al soltar el dedo.
- Tap normal: movimiento máximo 18 px y duración máxima 450 ms.
- En preview: tap de salida tolera hasta 24 px y 700 ms.
- Se contabilizan taps y swipes aceptados para diagnóstico.

## Métricas nuevas

`/api/status` y Administración muestran:

- `camera_jpeg_quality`
- `camera_fb_count`
- `camera_grab_mode`
- `preview_capture_ms`
- `preview_decode_ms`
- `preview_frame_ms`
- `preview_pipeline_fps`
- `preview_display_ms`
- `preview_loop_ms`
- `preview_fps` — FPS reales aproximados del ciclo completo
- `touch_poll_ms`
- `touch_swipe_threshold_px`
- `touch_debounce_ms`
- `touch_taps`
- `touch_swipes`

## Prueba física de aceptación

1. Confirmar portada `BUILD 005` y navegación base.
2. Hacer al menos 10 swipes seguidos y verificar que no haya doble navegación ni tap residual.
3. Entrar y salir del preview al menos 10 veces con tap corto.
4. Mantener preview 2 minutos y observar `preview_fps`, `preview_loop_ms`, `preview_capture_ms`, `preview_decode_ms` y `preview_display_ms`.
5. Confirmar que Wi‑Fi y `/admin` siguen accesibles durante y después del preview.
6. Comparar FPS reales contra Build 004 (~17 FPS informados por el pipeline previo).
7. Si hay resets, corrupción visual o pérdida de touch, no avanzar a streaming.

## Fuera de alcance

- Streaming MJPEG por Wi‑Fi.
- Grabación.
- Efectos.
- Detección de rostro.
- Overlays.

Si Build 005 queda estable físicamente, Build 006 puede habilitar el primer stream MJPEG IP con métricas de clientes, FPS y memoria.
