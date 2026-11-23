idf_component_register(
    SRCS "app_main.cpp" "commissionable_data.cpp" "matter_setup.cpp" "ota.cpp" "purifier.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES levoit nvs_flash esp_http_server app_update esp_wifi esp_timer
)
