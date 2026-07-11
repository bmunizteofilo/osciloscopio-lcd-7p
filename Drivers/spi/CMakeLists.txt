idf_component_register(
    SRCS "src/driver_spi.c"
    INCLUDE_DIRS
        "include"
        "../gpio/include"
    REQUIRES esp_driver_spi gpio
)
