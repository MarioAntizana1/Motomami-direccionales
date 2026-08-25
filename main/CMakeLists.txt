idf_component_register(SRCS "main.c" "led_strip_encoder.c" "ota_server.c"
                    PRIV_REQUIRES esp_driver_rmt esp_driver_gpio esp_wifi esp_netif nvs_flash esp_event
                                  esp_http_server app_update esp_timer
                    INCLUDE_DIRS ".")