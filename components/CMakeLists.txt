idf_component_register(SRCS "http.c" "wifi.c" "ir_nec_decoder.c" "servo.c" "cJSON.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_rmt esp_driver_mcpwm esp_driver_gpio esp_event nvs_flash esp_netif esp_wifi esp_http_client esp-tls)