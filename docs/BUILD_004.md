# Build 004 — Local live preview

## Objetivo

Validar que el appliance puede tomar frames reales de la cámara OV5640, decodificar JPEG QVGA a RGB565 y mostrarlos continuamente en la LCD sin habilitar todavía streaming por red.

## Alcance

- Conserva Build 001–003: LCD/touch, glitch, Wi-Fi AP, administración, identificación de cámara y captura de diagnóstico.
- Cámara configurada en JPEG QVGA 320×240, calidad 12, un framebuffer.
- El preview se activa sólo desde la segunda pantalla del appliance con `TAP TO PREVIEW`.
- Durante preview, la LCD cambia temporalmente a geometría lógica 320×240 y presenta el frame a pantalla completa.
- El JPEG se decodifica localmente a RGB565 usando el conversor de `esp32-camera` 2.0.15.
- Un toque durante el preview vuelve al diagnóstico y restaura 240×320.
- Si falla captura, formato, tamaño o decodificación, el preview se cancela y el error queda expuesto en `/api/status` y administración.
- Streaming MJPEG/IP, grabación, efectos y overlays continúan deshabilitados.

## Diagnóstico agregado

`/api/status` incorpora:

- `preview`: pending / unavailable / available / active / ready
- `preview_frames`
- `preview_capture_ms`
- `preview_decode_ms`
- `preview_frame_ms`
- `preview_fps`
- `preview_error`

## Prueba física

1. Flashear el factory BIN del Build 004.
2. Confirmar portada `PRISMA P · CAMERA · BUILD 004`.
3. Deslizar a la segunda pantalla.
4. Confirmar `CAMERA READY` y `LIVE PREVIEW READY`.
5. Tocar `TAP TO PREVIEW`.
6. Verificar que aparezca imagen real de cámara ocupando la pantalla y que se actualice durante varios segundos.
7. Tocar nuevamente y confirmar retorno al diagnóstico.
8. Abrir `192.168.4.1/admin` y revisar `VIDEO & STREAM`: `preview_frames > 0`, tiempos de captura/decode/frame y `preview_error = none`.
9. Confirmar que Wi-Fi y administración siguen respondiendo después de entrar y salir del preview varias veces.

## Criterio de aprobación

Build 004 se considera físicamente aprobado sólo si:

- cámara y captura inicial siguen en `ready`;
- el preview muestra frames reales y consecutivos;
- el touchscreen permite entrar y salir sin reiniciar;
- la orientación es utilizable y los colores son correctos;
- no hay bloqueo de LCD, reinicio, watchdog ni pérdida de administración;
- `/api/status` registra frames y no informa error de preview.

La tasa de FPS del Build 004 es diagnóstica. No se asume todavía que sea la configuración final de video ni de streaming.
