#pragma once

#include "esp_err.h"
#include "gt911_touch.h"
#include "wt32s3_lcd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa a porta LVGL usando os drivers de LCD e touch do projeto.
 *
 * @param[in] lcd Handle do driver do LCD RGB.
 * @param[in] touch Handle do driver de touch GT911.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t app_lvgl_init(wt32s3_lcd_handle_t lcd, gt911_touch_handle_t touch);

#ifdef __cplusplus
}
#endif
