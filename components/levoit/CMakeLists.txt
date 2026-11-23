idf_component_register(
    SRCS "src/filter_life.cpp" "src/levoit_core.cpp" "src/levoit_frame.cpp" "src/levoit_link.cpp" "src/purifier_map.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_uart esp_timer
)
