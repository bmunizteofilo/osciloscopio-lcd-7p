idf_component_register(
    SRCS "src/driver_bluetooth.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash
)
