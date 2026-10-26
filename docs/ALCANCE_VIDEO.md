# Alcance del appliance de video — propuesta v0.1

Fecha: 2026-10-26. Documento de diseño, no declaración de funciones implementadas.
Objetivo confirmado por el usuario: capturar video en vivo, transmitirlo, aplicar parte del procesamiento en el appliance y parte en el navegador.
Base física: Waveshare ESP32-S3-Touch-LCD-2, cámara del kit pendiente de identificación por software.
Referencia funcional: PRISMA-PI-LITE; revisar código exacto antes de portar. Proyecto y builds independientes.

## Criterio de alcance

N = núcleo a implementar; C = función creativa incluida en el diseño cuya capacidad debe validarse; R = extensión reservada, fuera de la primera versión. Todas están pendientes de implementación.
Cerrar el inventario ahora no permite garantizar FPS, resolución o número de capas sin medir. Un límite no alcanzable se hará visible antes de cerrar la versión.

## Recorridos de imagen

- SOURCE: salida JPEG del sensor después de sus ajustes. No es RAW ni una segunda cámara independiente.
- DEVICE: resultado de procesamiento de píxeles en ESP32, cuando el perfil lo habilite.
- PROGRAM: composición final del browser.

Perfil de transporte: cámara JPEG -> distribución de frames -> stream y grabación SOURCE en SD; preview local reducido se decodifica por separado.
Perfil local creativo: frame -> decodificación/RGB -> efecto local -> preview; para enviarlo o grabarlo como DEVICE se debe codificar JPEG otra vez. Medir costo antes de habilitar simultaneidad.
Browser: SOURCE o DEVICE -> decodificación -> efectos/overlays/loops -> PROGRAM -> pantalla o grabación.
No asumir SOURCE y DEVICE simultáneos a plena tasa. Una única tarea posee la captura; consumidores reciben referencias o copias con vida útil explícita.
Los efectos de browser no aparecen automáticamente en LCD ni en SD del appliance. El retorno PROGRAM al equipo es R, requiere protocolo, ancho de banda y decodificación adicionales.

## Inventario funcional

| ID | Función | Dónde se ejecuta | Alcance y aceptación |
|---|---|---|---|
| N01 | Identificación y diagnóstico | ESP32 + web | Sensor, Flash, PSRAM, periféricos y errores reales; fallo parcial no debe bloquear administración. |
| N02 | Captura continua | Sensor + ESP32 | Inicio/parada; perfiles QVGA/VGA de prueba; resolución real reportada. |
| N03 | Ajustes de cámara | Sensor, control local/web | Calidad, exposición/ganancia auto/manual, WB, brillo, contraste, saturación, espejo, flip según soporte real. |
| N04 | Preview táctil | ESP32 | Escalado/recorte/orientación; preview desacoplado del FPS de captura; poder apagarlo para ahorrar recursos. |
| N05 | Streaming local | ESP32 -> browser | MJPEG/HTTP inicial, un cliente de video objetivo; desconectar cliente lento sin agotar buffers. |
| N06 | Controller y administración | ESP32 sirve web; browser renderiza | Operación sin Internet/CDN. Acceso a estado, controles y diagnósticos. |
| N07 | Estado común | ESP32 + browser | Revisión de estado, comandos validados y confirmación; cambios locales visibles remotamente; orden definido ante controles concurrentes. |
| N08 | AP/STA y recuperación | ESP32 | Selección de red, credenciales persistentes protegidas, timeout/reconexión y AP de recuperación. |
| N09 | Nombre de máquina | ESP32 | Hostname editable, mDNS HTTP y nombre único; probar .local en AP/STA y conservar acceso por IP. |
| N10 | ESP-NOW | ESP32/pares | Pairing explícito, ID persistente, versión, secuencia, ACK de ejecución, timeout, reintentos acotados, deduplicación y canal compatible con STA. Sin video por este transporte. |
| N11 | Captura de foto | ESP32 y browser | Elegir SOURCE/DEVICE/PROGRAM según disponibilidad; indicar destino y resolución. |
| N12 | Grabación de origen | ESP32 + microSD | Secuencia JPEG con índice/timestamps como primer formato candidato; REC/STOP, espacio libre, nombres únicos, cierre consistente y recuperación. Formato final depende de validación. |
| N13 | Grabación de composición | Browser | PROGRAM vía canvas/captureStream/MediaRecorder si compatible; negociar MIME, iniciar/parar, descargar y reproducir archivo. No prometer MP4 universal. |
| N14 | Biblioteca | SD + browser | Listado, miniaturas, descarga, borrado explícito, espacio y errores. Aplazar descargas grandes si perjudican stream. |
| N15 | Presets | ESP32 + browser | Separar parámetros de sensor/local/browser, formato versionado, guardar/cargar/importar/exportar y restaurar valores por defecto. |
| N16 | Salud del pipeline | ESP32 + browser | FPS capturado/transmitido/renderizado separados, tamaño JPEG, drops, memoria mínima, colas, tiempo por etapa y estado de grabación. |
| N17 | IMU | ESP32 | Lectura/calibración; asignación configurable a parámetros; filtro y límite de frecuencia. |
| N18 | Energía | ESP32 | Voltaje, brillo e inactividad; sin dormir ni apagar captura durante REC; porcentaje solo si estimado/calibrado. |
| N19 | Administración y acceso | ESP32 + browser | Credenciales, sesiones, validación de entradas, configuración exportable sin secretos y reset explícito. Exposición inicial solo LAN. |
| N20 | Mantenimiento | ESP32 + CI | Versiones fijadas, build reproducible, artefactos y procedimiento USB de recuperación. Prever particiones para OTA y validar rollback antes de habilitar actualización web. |
| C01 | Efectos locales | ESP32 | Primer candidato: pixelado o umbral sobre frame reducido; bypass y costo medido. No solo overlay de UI. |
| C02 | Overlays locales | ESP32 | Texto/sprite; distinguir overlay LCD y efecto incorporado en DEVICE; probar recomprimir. |
| C03 | Cadena visual | Browser | Pixel, posterización, paleta, hue, espejo, glitch; orden, enable, intensidad y bypass por efecto. Canvas/WebGL según capacidad. |
| C04 | Capas | Browser | Cámara + clips + texto/sprites; opacidad, orden y transformación; máximo de capas según perfil medido. |
| C05 | Chroma key | Browser | Selección de color, tolerancia y suavizado; evaluar contornos, costo y exportación. |
| C06 | Looper | Browser; variante local a evaluar | REC/STOP/PLAY, IN/OUT, duración acotada, velocidad y opacidad. Loop inverso/ping-pong solo si almacenamiento/decodificación lo permiten. |
| C07 | Rostros y máscaras | Browser primero | Detección/tracking, recorte rectangular y overlay; modelos locales si se exige offline. Segmentar silueta/cabello es una capacidad distinta. |
| C08 | Detección local | ESP32 | Prototipo separado con ESP-WHO/ESP-DL, resolución/frecuencia reducida. Admitir que no conviva con todos los modos. |
| C09 | Salida visual limpia | Browser | Vista PROGRAM sin controles para pantalla externa del host; fullscreen con gesto y compatibilidad verificada. |
| R01 | BLE/MIDI | ESP32 | Reservar interfaces de comandos; no mantener radio BLE activo por defecto durante streaming. |
| R02 | Más espectadores/nodos de video | Host/browser/gateway | No prometer escalabilidad multiusuario del servidor de ejemplo. |
| R03 | WebRTC/RTSP/Internet | Gateway/host | Evaluación posterior; MJPEG local no equivale a estos protocolos. |
| R04 | Retorno PROGRAM al LCD/SD | Browser -> ESP32 | Extensión explícita, sin asumir que el equipo ya recibe la composición final. |
| R05 | Audio sincronizado, HDMI/USB-video, autofocus | Por definir | No confirmados en esta integración; no exponer controles ficticios. |

## UI prevista

Appliance: LIVE, CAMERA, FX LOCAL, REC/LOOPS, PRESETS, NETWORK/NODES y SYSTEM; navegación adaptada al panel pequeño.
Browser: SOURCE, FX, LAYERS, LOOPS, PROGRAM y ADMIN.
Siempre indicar origen/destino de REC, procesamiento activo (DEVICE/BROWSER), resolución, conexión y fallos. Un control remoto de un efecto no cambia su lugar de ejecución.
Sugerencia inicial de uso: cámara + efecto local ligero + efecto browser + grabación PROGRAM. Standalone conserva captura/control y las funciones locales validadas.

## Reglas de implementación que evitan rehacer el proyecto

- Modelo de estado único del dispositivo; estado de composición por sesión browser y presets explícitos. No escribir Flash en cada movimiento de slider.
- Captura, pantalla, red, SD y comandos desacoplados; colas acotadas. Priorizar frame reciente para LIVE.
- Grabación tiene política propia de drops y timestamps: jamás acumular RAM sin límite intentando no perder frames.
- Reservar recursos LEDC distintos para XCLK y brillo. Administrar SPI LCD/SD e I2C compartido.
- Cambiar resolución/formato con transición controlada: rechazar durante grabación o detener/cerrar de forma explícita.
- Cargar UI/modelos/assets desde el equipo o paquete local. No depender de Internet mientras se está conectado a su AP.
- Si UI y stream usan distintos puertos son distintos orígenes: configurar CORS y carga de imágenes para mantener canvas exportable. Preferir mismo origen cuando el servidor lo permita.
- Una futura web HTTPS remota requiere resolver contenido mixto/acceso a LAN; no suponer que basta CORS. Primera versión: UI local del appliance.
- Medir latencia visual extremo a extremo con referencia común; timestamps entre dispositivos sin sincronización no bastan.
- Pausar, avisar o recuperar explícitamente al suspender el browser, bloquear el móvil o perder WebGL. No garantizar grabación con pestaña cerrada o teléfono bloqueado.
- No añadir sensores o salidas no presentes en el equipo para imitar páginas del appliance anterior.

## Puertas de validación

G0: reproducir ejemplos, identidad del sensor, memoria, GPIO y periféricos.
G1: JPEG -> browser a 320×240; objetivo provisional de evaluación 15 FPS y latencia visual p95 <=300 ms en LAN próxima, no garantía. Medir también VGA sin comprometerla como requisito.
G2: sumar preview, control y ESP-NOW; demostrar que brillo no rompe cámara y que comandos llegan durante stream.
G3: grabación SD + stream durante 30 minutos, sin reinicio ni caída sostenida de memoria; registrar drops y probar SD llena/ausente.
G4: efecto local real + stream; comparar contra bypass, declarar FPS alcanzable.
G5: composición browser + grabación: Safari/iPhone, Chrome/Android y escritorio; comprobar reproducción/exportación, color/orientación y recuperación de red.
G6: pruebas de reinicio, reconexión, nombres duplicados, dos controles concurrentes y pérdida de peers. Prueba extendida de 2 horas del perfil elegido antes de considerarlo estable.

Los tiempos y objetivos de G1/G3/G6 son criterios propuestos del proyecto, no cifras del fabricante.
Antes de congelar v1 hay que decidir con mediciones: resolución/FPS garantizados, número de capas, duración de loops, formato de SD, formato de exportación por navegador y combinaciones simultáneas permitidas.

## Evidencia

Ver [revisión de código oficial](REVISION_OFICIAL.md). Ninguna función de esta matriz se ha compilado ni validado físicamente todavía.
