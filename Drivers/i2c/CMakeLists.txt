idf_component_register(
    SRCS "src/driver_i2c.c"
    INCLUDE_DIRS
        "include"
        "../../Drivers/gpio/include"
        "../../main"
        "../../components/aw9523b/include"
        "../../components/gt911_touch/include"
        "../../components/wt32s3_lcd/include"
    REQUIRES esp_driver_i2c gpio
)
