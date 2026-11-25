idf_component_register(
    SRCS "main.c" "node_table.c"
    INCLUDE_DIRS "."
    REQUIRES safenode_proto safenode_bsp sx1276 driver esp_wifi esp_event esp_netif
             nvs_flash esp_timer esp_app_format)
