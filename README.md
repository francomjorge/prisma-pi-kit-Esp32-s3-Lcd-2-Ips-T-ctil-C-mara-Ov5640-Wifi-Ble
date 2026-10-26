# PRISMA PI — ESP32-S3 Touch LCD 2

Appliance independiente basado en Waveshare ESP32-S3-Touch-LCD-2. Modelo y resolución 240×320 confirmados visualmente en la placa del usuario. Cámara instalada; OV5640 según el kit, pendiente de verificar por identificación del sensor en firmware.

## Estado

Planificación y documentación inicial. Este repositorio todavía no contiene firmware, workflow de compilación ni binario validado. Ninguna prueba de hardware se ha ejecutado desde este proyecto.

## Base funcional

Tomar como referencia funcional PRISMA-PI-LITE, revisando su código y versión antes de portar funciones: interfaz local, Controller web, administración, modos, parámetros y sincronización de estado. Este appliance tendrá drivers, configuración y builds propios. No modificar el repositorio ni los builds del appliance anterior.

## Primer firmware: diagnóstico

- Detectar procesador, tamaño de Flash y PSRAM.
- Verificar LCD, orientación, brillo y coordenadas táctiles.
- Identificar cámara y presentar imagen local.
- Probar lectura/escritura microSD sin sobrescribir archivos existentes.
- Mostrar aceleración y giro de la IMU.
- Mostrar voltaje de batería; no confundir voltaje con porcentaje exacto.
- Implementar y probar Wi-Fi AP/STA y reconexión.
- Implementar nombre editable y mDNS; nombre propuesto: prisma-cam-01.local. Mantener IP visible en diagnóstico.
- Incorporar ESP-NOW y comprobar envío/recepción con un segundo nodo.

## Diseño de comunicaciones

ESP-NOW transportará comandos, eventos y estado. Wi-Fi/IP transportará imágenes, archivos y streaming. Coordinar canal de radio entre STA/AP y peers ESP-NOW. Definir versión de protocolo, identificación de nodos, secuencias, confirmación de ejecución, reintentos y descarte de duplicados.

Probar mDNS en AP y STA, tras reinicios y reconexiones, y desde los dispositivos cliente reales. No dar por probado el acceso por nombre por el solo hecho de configurar un hostname.

## Etapas

1. Descargar y revisar ejemplos oficiales y esquema; documentar pines y recursos compartidos.
2. Fijar versiones reproducibles de ESP-IDF y componentes según los ejemplos verificados.
3. Compilar y validar periféricos por separado.
4. Integrar diagnóstico, interfaz y configuración persistente.
5. Validar conectividad y ESP-NOW.
6. Integrar captura, vista local y streaming midiendo FPS y memoria.
7. Evaluar efectos, overlays, almacenamiento de secuencias y loops. Capacidad y rendimiento pendientes de medición.

OTA, BLE/MIDI y detección de rostros quedan como ampliaciones a diseñar y validar. No se presupone audio integrado ni compatibilidad directa de ESP-WHO con esta placa.

## Fuentes oficiales

- [Placa Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2)
- [Recursos, ejemplos y documentos](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2/Resources-And-Documents)
- [Esquema eléctrico](https://files.waveshare.com/wiki/ESP32-S3-LCD-2/ESP32-S3-Touch-LCD-2-SchDoc.pdf)
- [Guía ESP-IDF](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2/ESP-IDF)
- [Guía Arduino](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2/Arduino)
- [ESP32-S3 datasheet](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf)
- [esp32-camera](https://github.com/espressif/esp32-camera)
- [ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html)
- [mDNS](https://docs.espressif.com/projects/esp-protocols/mdns/docs/latest/en/index.html)
- [ESP-WHO](https://github.com/espressif/esp-who)

Las referencias stable/latest pueden cambiar. Al implementar se fijarán versiones concretas. El paquete ZIP oficial aún no ha sido inspeccionado en esta fase.
