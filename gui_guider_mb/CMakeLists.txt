# Compila a interface gerada pelo NXP GUI Guider usando a LVGL do projeto.
# A pasta platform/ contém apenas a cópia da LVGL e o simulador desktop do GUI
# Guider; ela é propositalmente excluída deste componente ESP-IDF.
file(GLOB_RECURSE GUI_GUIDER_SOURCES
    "generated/*.c"
    "custom/*.c"
)

idf_component_register(
    SRCS ${GUI_GUIDER_SOURCES}
    INCLUDE_DIRS
        "."
        "generated"
        "generated/events"
        "generated/assets/fonts"
        "generated/assets/images"
        "custom"
    REQUIRES lvgl__lvgl
)
