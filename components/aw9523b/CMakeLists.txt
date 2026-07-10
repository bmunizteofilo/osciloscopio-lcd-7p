idf_component_register(
    SRCS "src/aw9523b.c"
    INCLUDE_DIRS
        "include"
        "../../Drivers/i2c/include"
        "../../Drivers/gpio/include"
        "../../main"
        "../gt911_touch/include"
        "../wt32s3_lcd/include"
    REQUIRES i2c gpio
)
