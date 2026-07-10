idf_component_register(
    SRCS "src/gt911_touch.c"
    INCLUDE_DIRS
        "include"
        "../../Drivers/i2c/include"
        "../../Drivers/gpio/include"
        "../../main"
        "../aw9523b/include"
        "../wt32s3_lcd/include"
    REQUIRES i2c gpio aw9523b
)
