idf_component_register(
    SRCS "main.c" "classifier.c" "sensors.c" "dht22.c" "outputs.c" "selftest.c"
         "radio.c" "eventlog.c" "console.c"
    INCLUDE_DIRS "."
    REQUIRES safenode_proto safenode_bsp sx1276 driver esp_adc esp_wifi esp_event esp_netif
             nvs_flash esp_timer esp_system esp_app_format)
