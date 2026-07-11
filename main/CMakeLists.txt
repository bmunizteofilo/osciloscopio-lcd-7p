idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS
        "."
        "../Drivers/i2c/include"
        "../Drivers/gpio/include"
        "../Drivers/spi/include"
        "../components/aw9523b/include"
        "../components/gt911_touch/include"
        "../components/wt32s3_lcd/include"
    REQUIRES i2c gpio spi aw9523b wt32s3_lcd gt911_touch app_lvgl
)
