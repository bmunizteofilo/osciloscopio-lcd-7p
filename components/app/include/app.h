#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa os serviços e periféricos que compõem a aplicação.
 *
 * Configura I2C, expansor de GPIO, painel LCD, touch, LVGL e o barramento SPI
 * da placa de aquisição, incluindo a supervisão de presença da STM32.
 *
 * @return @c ESP_OK em caso de sucesso ou o primeiro erro de inicialização.
 */
esp_err_t app_init(void);

#ifdef __cplusplus
}
#endif
