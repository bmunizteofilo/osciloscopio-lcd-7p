idf_component_register(
    SRCS "src/app_lvgl.c"
    INCLUDE_DIRS
        "include"
        "../../Drivers/i2c/include"
        "../../Drivers/gpio/include"
        "../../main"
        "../aw9523b/include"
        "../gt911_touch/include"
        "../wt32s3_lcd/include"
    REQUIRES lvgl esp_timer wt32s3_lcd gt911_touch
)
