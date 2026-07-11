idf_component_register(
    SRCS "src/osc.c"
    INCLUDE_DIRS
        "include"
        "../wt32s3_lcd/include"
        "../../managed_components/lvgl__lvgl/src"
    REQUIRES lvgl esp_timer wt32s3_lcd gt911_touch
)
