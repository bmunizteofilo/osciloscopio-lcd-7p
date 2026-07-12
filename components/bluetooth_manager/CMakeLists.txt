idf_component_register(
    SRCS "src/bluetooth_manager.c"
    INCLUDE_DIRS "include" "../../Drivers/bluetooth/include"
    REQUIRES bluetooth freertos
)
