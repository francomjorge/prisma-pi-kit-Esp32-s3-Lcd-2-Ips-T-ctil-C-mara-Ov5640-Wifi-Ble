# PRISMA-PI CAMERA — Build 008B

## Objetivo
Build 008B parte de 008A / núcleo funcional de Build 006 y prioriza explícitamente el streaming web MJPEG sobre el preview local del appliance.

## Cambios permitidos en esta fase
- Pipeline de cámara para evitar que el decode local retenga el mutex de captura.
- Métricas de stream y de preview local.
- Prioridad/tamaño de stack del servidor HTTP de stream.
- Panel web de diagnóstico.
- Versionado y artefactos.

## Cambios deliberadamente NO realizados
- No se cambia la resolución: sigue VGA 640x480.
- No se cambia JPEG quality: sigue 10.
- No se cambia `fb_count=2` con PSRAM ni `CAMERA_GRAB_LATEST`.
- No se cambia la lógica de touch/swipe del Build 006.
- No se cambia Wi-Fi/AP en esta fase.
- No se agregan efectos, grabación ni controles de sensor.

## Stream priority
El Build 006 retenía `camera_lock` durante el decode JPEG -> RGB565 usado por la LCD. Mientras ese decode estaba activo, el servidor MJPEG podía quedar esperando la cámara.

008B agrega un buffer de staging JPEG de 384 KiB en PSRAM:
1. LCD toma un JPEG VGA.
2. Copia el JPEG al staging buffer.
3. Devuelve inmediatamente el framebuffer al driver.
4. Libera `camera_lock`.
5. Decodifica la copia a 320x240 fuera del lock.

Si el staging buffer no puede reservarse, el firmware conserva automáticamente el comportamiento anterior como fallback.

## Servidor MJPEG
- Puerto 81.
- Un viewer simultáneo, igual que Build 006.
- Stack 10 KiB.
- Prioridad de tarea 6.
- Sin delay artificial de 1 ms entre frames; se usa `taskYIELD()`.
- Hasta 5 errores consecutivos de captura antes de cerrar el stream.

## Telemetría nueva
`/api/status` agrega:
- `stream_priority`
- `stream_capture_ms`
- `stream_send_ms`
- `stream_dropped`
- `preview_staging`
- `preview_copy_ms`

`stream_fps` pasa a calcularse sobre una ventana aproximada de 1 segundo, no como `1000 / último_frame_ms`.

## Prueba física recomendada
1. Iniciar sin preview local y abrir el stream web durante 2 minutos.
2. Registrar `stream_fps`, `stream_capture_ms`, `stream_send_ms`, `stream_dropped`.
3. Sin cerrar el stream, abrir el preview local en el appliance.
4. Verificar que el stream mantiene fluidez razonable aunque baje el FPS local.
5. Confirmar `preview_staging=true` en `/api/status`.
6. Verificar que `/admin` y `/api/status` siguen respondiendo durante el stream.

## Criterio de avance a 008C
Avanzar sólo si el stream web se mantiene estable con y sin preview local y no aparecen resets, corrupción JPEG o pérdida de administración.
