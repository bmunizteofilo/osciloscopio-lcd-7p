idf_component_register(
    SRCS "src/wifi_manager.c"
    INCLUDE_DIRS "include" "../../Drivers/wifi/include"
    REQUIRES wifi freertos
)
