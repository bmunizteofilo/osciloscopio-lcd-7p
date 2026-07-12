#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "wt32s3_lcd.h"

/** @brief Níveis disponíveis para volume e beep. */
typedef enum {
    GENERAL_SETTINGS_VOLUME_LOW,
    GENERAL_SETTINGS_VOLUME_MEDIUM,
    GENERAL_SETTINGS_VOLUME_HIGH
} general_settings_volume_t;

/** @brief Unidades disponíveis para apresentar a pressão. */
typedef enum {
    GENERAL_SETTINGS_PRESSURE_BAR,
    GENERAL_SETTINGS_PRESSURE_PSI
} general_settings_pressure_unit_t;
/** @brief Inicializa os parâmetros gerais vinculados ao LCD. */
esp_err_t general_settings_init(wt32s3_lcd_handle_t lcd);
/** @brief Ajusta o brilho entre 10 e 100 por cento. */
esp_err_t general_settings_set_brightness(uint8_t percent);
/** @brief Retorna o brilho configurado. */
uint8_t general_settings_get_brightness(void);
/** @brief Ajusta o nível de volume e beep. */
void general_settings_set_volume(general_settings_volume_t volume);
/** @brief Retorna o nível de volume e beep. */
general_settings_volume_t general_settings_get_volume(void);
/** @brief Ajusta a unidade usada para apresentar pressão. */
void general_settings_set_pressure_unit(general_settings_pressure_unit_t unit);
/** @brief Retorna a unidade usada para apresentar pressão. */
general_settings_pressure_unit_t general_settings_get_pressure_unit(void);
/** @brief Retorna o idioma disponível atualmente. */
const char *general_settings_get_language(void);
