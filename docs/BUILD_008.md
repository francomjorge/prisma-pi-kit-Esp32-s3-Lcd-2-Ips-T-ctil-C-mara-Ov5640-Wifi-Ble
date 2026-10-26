# BUILD 008A — STABLE BASELINE / STREAM-FIRST

## Objetivo

Retomar el comportamiento operativo del Build 006, que fue la versión físicamente validada con mejor equilibrio hasta ahora para PRISMA-PI CAMERA.

Build 008A no intenta mejorar touch ni cambiar el pipeline de cámara. Su función es crear una base nueva, reproducible y claramente versionada para continuar el desarrollo sin arrastrar los experimentos del Build 007.

## Base técnica congelada desde Build 006

- ESP-IDF 5.3.2.
- OV5640 en JPEG VGA 640×480.
- JPEG quality 10.
- 2 framebuffers en PSRAM cuando está disponible.
- CAMERA_GRAB_LATEST.
- Preview local VGA → 320×240 mediante JPG_SCALE_2X.
- Stream MJPEG directo en puerto 81.
- Administración HTTP en puerto 80.
- Un viewer de stream simultáneo.
- /api/status con telemetría de dispositivo, cámara, preview, touch y stream.
- Lógica de display/touch exactamente igual a Build 006.

## Cambios de 008A

- Versión firmware 008.0.
- Branding web estandarizado como PRISMA-PI CAMERA.
- Panel de administración reorganizado y ampliado.
- Funciones futuras visibles como planificadas/grisadas, sin ejecutar acciones inexistentes.
- Roadmap explícito orientado a estabilidad y rendimiento de streaming.

## Regla de prioridad

El stream web es la salida de video prioritaria. El preview local de la LCD puede operar a menor FPS y no debe condicionar el objetivo de fluidez del stream.

## Roadmap

### 008A — Stable baseline
Restaurar el comportamiento del Build 006 y validar que el stream vuelve a su funcionamiento conocido.

### 008B — Stream priority
Optimizar exclusivamente streaming: frame pacing, latencia, buffers, métricas de frame age/drop, snapshot JPEG y política de convivencia con preview local.

### 008C — Camera controls
Exponer por web resolución, JPEG quality, exposición, AEC/AGC/AWB, gain, brightness, contrast, saturation, sharpness, mirror y flip, con persistencia y defaults seguros.

### 008D — Network
AP abierto/protegido configurable, SSID/password, Wi-Fi STA, reconexión y mDNS.

### 008E — System/admin
Logs, reboot, factory reset, OTA, backup/restore, storage, presets y diagnóstico extendido.

### 009+
Recording, loops, efectos, procesamiento, overlays, detecciones y nodos distribuidos.

## Criterio de aceptación de 008A

1. Boot correcto.
2. Administración disponible en puerto 80.
3. /api/status responde de forma estable.
4. Stream en :81/stream con comportamiento equivalente al Build 006 cuando el preview local está cerrado.
5. Ninguna regresión nueva causada por cambios de touch/cámara, porque esos bloques permanecen congelados.
