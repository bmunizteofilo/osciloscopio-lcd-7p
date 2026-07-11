#pragma once

#include "esp_err.h"
#include "wt32s3_lcd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cria e carrega a interface do osciloscópio.
 *
 * @param[in] lcd Handle do painel usado para controlar o backlight.
 * @return ESP_OK em sucesso ou ESP_ERR_NO_MEM se a tela não puder ser criada.
 */
esp_err_t osc_create(wt32s3_lcd_handle_t lcd);

#ifdef __cplusplus
}
#endif
