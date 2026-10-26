# PRISMA-PI CAMERA

Firmware para el appliance ESP32-S3 + LCD táctil 240×320 + cámara OV5640.

## Línea estable actual

**Build 008A** retoma deliberadamente el comportamiento operativo del **Build 006**, que fue la referencia física con mejor funcionamiento general y streaming web fluido cuando el preview local estaba cerrado.

La rama `build008-stream-first` conserva el commit exacto del Build 006 como referencia dorada. La rama `build008a-stable-baseline` desarrolla la nueva línea sin reescribir la historia de `main`.

## Base de video

- OV5640.
- JPEG VGA 640×480.
- JPEG quality 10.
- 2 framebuffers en PSRAM.
- `CAMERA_GRAB_LATEST`.
- Stream MJPEG directo: `http://<ip>:81/stream`.
- Administración: `http://<ip>/admin`.
- Estado: `http://<ip>/api/status`.
- Preview LCD: VGA → 320×240.

## Prioridad de producto

El **stream web es prioritario**. La LCD local puede usar menor FPS si eso evita degradar el stream. Touch queda fuera de la optimización inmediata; se conserva el comportamiento del Build 006 hasta que el pipeline web esté estable.

## Roadmap inmediato

1. **008A** — baseline Build 006 + panel de administración completo visualmente.
2. **008B** — stream priority, frame pacing, latencia, buffers y métricas.
3. **008C** — controles OV5640 desde web.
4. **008D** — AP/STA, red abierta/protegida y mDNS.
5. **008E** — sistema, logs, OTA, storage, presets y recuperación.
6. **009+** — recording, loops, procesamiento, overlays y nodos.

Ver `docs/BUILD_008.md` para criterios de aceptación y alcance.

## Historial relevante

- Build 004: primer preview local real físicamente validado.
- Build 005: optimización de LCD/touch.
- Build 006: VGA + MJPEG web; referencia funcional elegida para continuar.
- Build 007: experimento de estabilización posterior; no se usa como base operativa.
- Build 008A: regreso controlado al Build 006 con nueva estructura de administración.
