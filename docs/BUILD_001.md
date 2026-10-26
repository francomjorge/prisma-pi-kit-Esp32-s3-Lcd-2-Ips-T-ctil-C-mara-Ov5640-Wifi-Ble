# Build 001.0 — base y portada

Objetivo: firmware independiente para Waveshare ESP32-S3-Touch-LCD-2 (240×320).
No inicializa cámara, SD ni IMU. No transmite video. No modifica el appliance Lite.

## Pantalla

PRISMA P / CAMARA. Paleta ámbar, menta, marfil, cobre y verde apagado.
Se conserva el gesto del código activo app_factory_v1.c del Lite: toque añade nueve bloques de glitch; swipe horizontal cambia de página y reinicia el efecto visual; volver muestra portada limpia. No es un reinicio del ESP32 ni un reset de configuración.
Tres páginas: portada, funciones de cámara grisadas, acceso administrativo (SSID, clave e IP).
El framebuffer se transfiere por franjas DMA y espera su finalización antes de reutilizarlas.
Pines LCD/touch de los ejemplos oficiales de la placa 2 pulgadas. No se reutiliza el power-hold GPIO2 de la placa Lite (aquí pertenece a cámara).

## Web

Conectar a PRISMA-CAM-xxxxxx con la clave generada en primer arranque y persistida en NVS, mostrada localmente. Abrir http://192.168.4.1/admin.
14 secciones disponibles para navegar; funciones pendientes deshabilitadas. Diagnóstico GET /api/status de solo lectura. HTML/CSS/JS integrados sin CDN. WPA2 protege el AP; autenticación administrativa, STA, mDNS y ESP-NOW pendientes.
La IP se consulta realmente en API. No hay POST de reinicio ni borrado. No se borra NVS automáticamente si falla.

## Compilación y flasheo

ESP-IDF 5.3.2 fijado en CI. idf.py build.
El workflow produce PRISMA-CAMERA-build-001-factory.bin y SHA256SUMS.txt.
Binario fusionado: flashear en dirección 0x0, exclusivamente en esta placa ESP32-S3 de 16 MB. No usar el binario del Lite.
Particiones OTA A/B reservadas; OTA no habilitado. Partición de almacenamiento reservada sin formatear.

## Prueba física pendiente

1. Arrancar por USB: título, color y geometría correctos.
2. Tocar varios puntos: glitch visible, un evento por toque, sin disparos por swipe.
3. Deslizar a página 2 y regresar: portada limpia, sin reinicio ni pérdida de Wi-Fi.
4. Llegar a página 3: leer SSID/clave, abrir web desde móvil y notebook.
5. Recorrer las 14 secciones sin Internet; pendientes grisados y sin acciones falsas.
6. Comparar Flash/PSRAM reportadas; desconectar/reconectar cliente: la web informa pérdida de conexión.
7. Reiniciar: clave de AP permanece; verificar 30 minutos sin reset ni memoria decreciente.

Compilar no valida pantalla, orientación táctil ni PSRAM en la unidad física. Se necesita confirmación del usuario antes del siguiente build.
