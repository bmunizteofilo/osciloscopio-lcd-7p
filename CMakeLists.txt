cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    Drivers/i2c
    Drivers/gpio
    Drivers/spi
    gui_guider_mb
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lcd_7p)
