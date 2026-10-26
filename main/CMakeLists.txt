idf_component_register(
    SRCS "main.c" "display.c" "network.c" "web.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server driver esp_lcd esp_timer heap spi_flash esp_system
    EMBED_TXTFILES "web/home.html" "web/admin.html" "web/camera-sync.html" "web/shared.css"
)
