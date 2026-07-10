idf_component_register(
    SRCS "src/wt32s3_lcd.c"
    INCLUDE_DIRS
        "include"
        "../../Drivers/i2c/include"
        "../../Drivers/gpio/include"
        "../../main"
        "../aw9523b/include"
        "../gt911_touch/include"
    REQUIRES esp_lcd esp_driver_gpio esp_driver_ledc heap i2c gpio aw9523b
)
