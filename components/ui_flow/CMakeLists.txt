idf_component_register(
    SRCS "src/ui_flow.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl gui_guider_mb wifi_manager bluetooth_manager date_time osciloscopio general_settings
)
