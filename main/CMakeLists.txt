idf_component_register(SRCS "alarm.c" "ir_nec_transceiver_main.c" "ir_nec_decoder.c" "servo.c" "alarm.c"
                       PRIV_REQUIRES esp_driver_rmt esp_driver_mcpwm esp_driver_gpio esp_event nvs_flash esp_netif esp_wifi
                       INCLUDE_DIRS ".")
