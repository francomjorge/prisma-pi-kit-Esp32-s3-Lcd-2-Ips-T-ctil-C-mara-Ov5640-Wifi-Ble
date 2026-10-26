idf_component_register(SRCS "main.c" "display.c" "camera.c" INCLUDE_DIRS "."
 REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server driver esp_lcd esp_timer heap spi_flash esp_app_format
 EMBED_TXTFILES "web/admin.html")
