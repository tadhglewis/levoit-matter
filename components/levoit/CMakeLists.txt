idf_component_register(
    SRCS "src/core_bridge.cpp" "src/levoit_frame.cpp" "src/levoit_link.cpp" "src/purifier_map.cpp" "src/purifier_model.cpp"
    INCLUDE_DIRS "include"
    REQUIRES levoit_core
    PRIV_REQUIRES esp_driver_uart esp_timer
)
