# Build 007 — Stabilization

Producto: **PRISMA-PI CAMERA**.

## Objetivo

Estabilizar la plataforma después de la validación física del Build 006, sin sumar funciones grandes. El Build 006 confirmó buena calidad y fluidez del stream web cuando el preview local está cerrado, pero mostró degradación severa al ejecutar ambos pipelines y peor respuesta táctil por el filtro de confirmación triple.

## Cambios

### Touch

- Se elimina la confirmación de 3 muestras de presión/liberación del Build 006.
- Se vuelve a lectura directa del CST816D, como base del Build 005.
- Poll táctil: 8 ms.
- Umbral horizontal de swipe: 28 px.
- Debounce posterior al swipe: 220 ms.
- Se mantiene espera de liberación después del swipe.
- Al entrar al preview se exige una liberación real antes de aceptar el tap de salida, pero sin filtrado triple.
- `/api/status` conserva contadores de taps, swipes y gestos rechazados.

### Video

- OV5640 mantiene JPEG VGA 640×480, quality 10, 2 framebuffers y `GRAB_LATEST` con PSRAM.
- El preview local copia el JPEG a un buffer propio, devuelve inmediatamente el framebuffer y libera el mutex de cámara antes de decodificar.
- El decode VGA→320×240 se realiza fuera del lock de cámara.
- Objetivo local adaptativo:
  - sin stream web: ~12 FPS máximo objetivo;
  - con stream web activo: ~4 FPS máximo objetivo.
- El stream web continúa en puerto 81 y administración en puerto 80.
- Un cliente MJPEG simultáneo.

### Orientación

- Se aplica rotación 180° en el sensor usando `vflip + hmirror`.
- Se expone `camera_rotation` en `/api/status`.
- Selector persistente 0°/180° queda pendiente.

### Red

- SSID: `PRISMA-PI-CAMERA-XXXXXX`.
- AP abierto por defecto, sin contraseña.
- WPA2/WPA3 configurable queda visible como pendiente en administración.

### Administración

- Nombre estandarizado: `PRISMA-PI CAMERA`.
- Se conserva la telemetría completa existente y se agregan:
  - `product`
  - `wifi_auth`
  - `camera_rotation`
  - `preview_target_fps`

## Prueba física

1. Encender y confirmar ausencia de reset loops.
2. Confirmar SSID `PRISMA-PI-CAMERA-XXXXXX` abierto.
3. Abrir `http://192.168.4.1/admin` y comprobar que todas las páginas/tabs web responden.
4. Verificar `/api/status` y telemetría de firmware, memoria, touch, cámara y stream.
5. Probar 20 taps sobre la pantalla inicial y registrar taps duplicados o perdidos.
6. Probar 20 swipes izquierda/derecha y registrar fallos/dobles cambios de página.
7. Abrir preview local sin stream y observar FPS/latencia.
8. Cerrar preview local y abrir stream web: validar fluidez, color, resolución y orientación.
9. Con stream activo, abrir preview local y confirmar que el stream sigue utilizable aunque el LCD quede limitado a ~4 FPS.
10. Confirmar orientación visual 180° corregida según montaje físico.
11. Dejar 15–30 min con stream activo y revisar heap mínimo, errores y resets.

## Criterio de avance

No avanzar a procesamiento, overlays o grabación hasta que navegación táctil, estado web y stream sean estables. Si el stream sigue degradándose con preview local aun con este desacople, el siguiente paso será compartir un único frame JPEG entre ambas salidas en vez de realizar dos adquisiciones independientes.
