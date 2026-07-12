idf_component_register(
    SRCS "src/date_time.c"
    INCLUDE_DIRS "include" "../wifi_manager/include"
    REQUIRES wifi_manager lwip freertos nvs_flash
)
