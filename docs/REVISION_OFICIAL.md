# Revisión de ejemplos oficiales — 2026-10-26

Estado: revisión estática de código; sin compilación ni prueba física. Esta revisión actualiza el estado inicial del README: el ZIP oficial ya fue descargado y sus ejemplos relevantes fueron inspeccionados.

## Procedencia reproducible

Paquete: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2/ESP32-S3-Touch-LCD-2-Demo.zip
Tamaño descargado: 68.497.354 bytes.
SHA256: f7fc300a7710b221a9bfe33fc0cae588fd03d02fc655d509e09069fd60475e48

Consultar índice oficial para detectar futuras revisiones:
https://docs.waveshare.com/ESP32-S3-Touch-LCD-2/Resources-And-Documents

No se incorporan bibliotecas de terceros al repositorio en esta revisión. Antes de reutilizar código se preservarán sus licencias y avisos.

## Código inspeccionado

| Archivo dentro del ZIP | Hallazgo |
|---|---|
| Arduino/examples/10_camera_web_server/10_camera_web_server.ino | Captura JPEG, PSRAM, dos buffers cuando está disponible y CAMERA_GRAB_LATEST. Inicializa UXGA y después selecciona QVGA. Wi-Fi STA. |
| Arduino/examples/10_camera_web_server/app_httpd.cpp | Snapshot, streaming multipart JPEG, controles del sensor, estado JSON y servidor separado de stream. |
| Arduino/examples/10_camera_web_server/camera_pins.h | Contiene perfiles genéricos; no usar un perfil ESP-EYE como pinout de esta placa. El .ino define explícitamente sus GPIO. |
| ESP-IDF/05_lvgl_camera/main/main.c | RGB565, HVGA, un buffer, descriptor de imagen 480×320; panel 240×320. No equivale a captura JPEG más streaming más preview. |
| ESP-IDF/05_lvgl_camera/main/idf_component.yml | LVGL ~8.4.0, cámara ^2.0.15, controlador CST816S ^1.0.3. |
| ESP-IDF/05_lvgl_camera/dependencies.lock | IDF 5.3.2, cámara 2.0.15, LVGL 8.4.0, touch 1.1.2, CST816S 1.0.5. |
| ESP-IDF/05_lvgl_camera/sdkconfig | Inicializado con IDF 5.3.2, Flash 16 MB, PSRAM octal a 80 MHz, CPU configurada a 160 MHz. |
| ESP-IDF/01_sd_card_test/main/sd_card_example_main.c | SD por SPI, comparte líneas de datos/reloj con LCD. |
| ESP-IDF/02_lvgl_qmi8658/main/bsp_qmi8658.c | IMU en el bus I2C utilizado por el táctil. |
| ESP-IDF/04_lvgl_battery/main/main.c | Medición por ADC1, canal 4, con calibración. |

La guía web actual indica IDF 5.5.0 o posterior, pero el ZIP inspeccionado fija 5.3.2 en su lock. No afirmar que el hardware exige 5.5 ni mezclar versiones silenciosamente. Reproducir primero el ejemplo con sus dependencias y luego evaluar migración con pruebas.

## Mapa extraído del código, pendiente de contraste eléctrico completo y prueba

| Recurso | GPIO / configuración |
|---|---|
| Cámara D0..D7 | 12, 13, 15, 11, 14, 10, 7, 2 |
| Cámara XCLK / PCLK | 8 / 9 |
| Cámara VSYNC / HREF | 6 / 4 |
| Cámara SCCB SDA / SCL | 21 / 16 |
| Cámara PWDN / RESET | 17 / -1 (sin pin de reset en la configuración) |
| SPI MOSI / MISO / CLK | 38 / 40 / 39 |
| LCD CS / DC / backlight | 45 / 42 / 1 |
| SD CS | 41 |
| I2C táctil / IMU SDA / SCL | 48 / 47 |
| Batería | ADC1 canal 4 |

Los GPIO accesibles en cabeceras no son necesariamente libres con cámara conectada.

## Puntos que hay que corregir o verificar al integrar

1. El ejemplo IDF asigna LEDC_CHANNEL_0 tanto a la cámara como al backlight, aunque usa temporizadores diferentes. Reservar canales y timers independientes; verificar que cambiar brillo no altere XCLK.
2. LVGL recibe un puntero al buffer de cámara y luego el ejemplo devuelve ese buffer al driver. La integración necesita propiedad y vida útil explícitas: copiar a un buffer de preview o retenerlo hasta terminar el consumo.
3. Dimensiones 480×320 de captura/descriptor frente a panel 240×320: implementar escalado/recorte y orientación coherentes con el touch.
4. LCD y SD requieren un único administrador del bus SPI, CS separados y arbitraje. No inicializar dos veces el mismo bus ni monopolizarlo grabando.
5. El ejemplo Wi-Fi espera indefinidamente al router. Añadir timeout, reconexión y AP de recuperación.
6. La cabecera X-Framerate: 60 del servidor no constituye una medición ni una promesa de 60 FPS.
7. Los controles del sensor están expuestos en código; su soporte efectivo se verificará por sensor y retorno de la API. No asumir autofocus operativo por llamarse OV5640.
8. No trasladar ajustes de PLL, registros crudos o XCLK a los controles normales del usuario.
9. No confundir superponer elementos LVGL en el LCD con incorporarlos al JPEG transmitido o grabado.

## Documentación complementaria

- Driver y recomendaciones de memoria: https://github.com/espressif/esp32-camera
- Ejemplo actualizado de servidor: https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/Camera/CameraWebServer
- Guía IDF de la placa: https://docs.waveshare.com/ESP32-S3-Touch-LCD-2/ESP-IDF
- Esquema: https://files.waveshare.com/wiki/ESP32-S3-LCD-2/ESP32-S3-Touch-LCD-2-SchDoc.pdf
- Captura de canvas: https://developer.mozilla.org/en-US/docs/Web/API/HTMLCanvasElement/captureStream
- Formato de grabación: https://developer.mozilla.org/en-US/docs/Web/API/MediaRecorder/isTypeSupported_static
- CORS/canvas: https://developer.mozilla.org/en-US/docs/Web/HTML/How_to/CORS_enabled_image

El driver advierte sobre presión de memoria con RGB/YUV y Wi-Fi. La estrategia inicial propuesta es JPEG de cámara para transporte, decodificación reducida para preview y procesamiento pesado en el navegador. El perfil con procesamiento de píxeles en ESP32 necesita su propia medición.
