idf_component_register(SRCS "main.c" "led_strip_encoder.c"
                    PRIV_REQUIRES esp_driver_rmt esp_wifi esp_netif nvs_flash esp_event
                    INCLUDE_DIRS ".")