idf_component_register(
    SRCS "src/driver_gpio.c"
    INCLUDE_DIRS
        "include"
        "../../Drivers/i2c/include"
        "../../main"
        "../../components/aw9523b/include"
        "../../components/gt911_touch/include"
        "../../components/wt32s3_lcd/include"
    REQUIRES esp_driver_gpio
)
