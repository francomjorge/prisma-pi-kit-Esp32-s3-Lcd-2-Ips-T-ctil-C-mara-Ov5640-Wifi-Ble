idf_component_register(
    SRCS "main.c" "settings.c" "board_bus.c" "board_i2c.c" "storage.c" "display.c" "network.c" "discovery.c" "camera_hw.c" "touch_hw.c" "imu_hw.c" "platform_log.c" "platform_api.c" "web.c" "files_api.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server driver esp_lcd esp_timer heap spi_flash esp_system esp_partition fatfs wear_levelling sdmmc mdns esp32-camera json
    EMBED_TXTFILES "web/home.html" "web/admin.html" "web/admin.css" "web/admin.js" "web/camera-synth.html" "web/shared.css"
)
