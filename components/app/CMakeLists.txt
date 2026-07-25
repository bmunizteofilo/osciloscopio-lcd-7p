idf_component_register(
    SRCS "src/app.c"
    INCLUDE_DIRS "include"
    REQUIRES i2c gpio spi aw9523b wt32s3_lcd gt911_touch app_lvgl wifi_manager date_time general_settings report_storage acquisition_stream acquisition_history nvs_flash
)
