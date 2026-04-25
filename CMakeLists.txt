idf_component_register(
    SRCS "ED_wifi.cpp"
    INCLUDE_DIRS "." "$ENV{ESP_HEADERS}"
    REQUIRES
        esp_wifi esp_event esp_netif
        nvs_flash esp_http_server
        mbedtls
        ED_SYS
        ED_NVS
)