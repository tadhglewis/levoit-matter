set(LEVOIT_SRC "${CMAKE_CURRENT_LIST_DIR}/../../third_party/levoit/components/levoit")

idf_component_register(
    SRCS
        "${LEVOIT_SRC}/levoit_device.cpp"
        "${LEVOIT_SRC}/decoder.cpp"
        "${LEVOIT_SRC}/core_status.cpp"
        "${LEVOIT_SRC}/vital_status.cpp"
        "${LEVOIT_SRC}/sprout_status.cpp"
        "${LEVOIT_SRC}/superior_status.cpp"
        "${LEVOIT_SRC}/core_commands.cpp"
        "${LEVOIT_SRC}/vital_commands.cpp"
        "${LEVOIT_SRC}/sprout_commands.cpp"
        "${LEVOIT_SRC}/superior_commands.cpp"
        "${LEVOIT_SRC}/tlv.cpp"
        "${LEVOIT_SRC}/levoit_message.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-format -Wno-unused-variable -Wno-unused-but-set-variable)
