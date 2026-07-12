#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "wt32s3_lcd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Define o painel LCD usado pelos controles do osciloscópio.
 *
 * A configuração não cria objetos LVGL nem aloca o histórico de amostras.
 *
 * @param[in] lcd Handle do painel RGB da aplicação.
 */
void osc_set_lcd(wt32s3_lcd_handle_t lcd);

/** @brief Função chamada pelo botão Menu do osciloscópio. */
typedef void (*osc_menu_callback_t)(void);

/**
 * @brief Define a ação executada pelo botão Menu do osciloscópio.
 *
 * @param[in] callback Função de retorno para a tela principal.
 */
void osc_set_menu_callback(osc_menu_callback_t callback);

/**
 * @brief Cria a interface do osciloscópio.
 *
 * @param[in] lcd Handle do painel usado para controlar o backlight ou @c NULL
 * quando o painel foi definido previamente por @ref osc_set_lcd.
 * @return ESP_OK em sucesso ou ESP_ERR_NO_MEM se a tela não puder ser criada.
 */
esp_err_t osc_create(wt32s3_lcd_handle_t lcd);

/**
 * @brief Destrói a tela do osciloscópio, seus timers e buffers de histórico.
 *
 * A tela não deve estar ativa no momento da chamada.
 */
void osc_destroy(void);

/**
 * @brief Retorna a tela criada pelo componente do osciloscópio.
 *
 * @return Ponteiro da tela LVGL ou @c NULL se @ref osc_create ainda não foi chamado.
 */
lv_obj_t *osc_get_screen(void);

/**
 * @brief Define a taxa de frames recebidos pela entrada de aquisição.
 *
 * Cada frame contém uma amostra para cada um dos quatro canais. Esta função
 * deve ser chamada no core 1 antes de encaminhar frames de um novo perfil ADC.
 *
 * @param[in] sample_rate_hz Taxa de frames por segundo do ADC.
 * @return @c ESP_OK em caso de sucesso ou @c ESP_ERR_INVALID_ARG se a taxa for zero.
 */
esp_err_t osc_set_input_sample_rate(uint32_t sample_rate_hz);

/**
 * @brief Insere um frame ADC de quatro canais no histórico do osciloscópio.
 *
 * Os valores devem ser conversões ADC de 12 bits alinhadas nos bits menos
 * significativos. A chamada deve ocorrer no core 1, pelo consumidor da fila
 * SPSC; a task SPI do core 0 não deve chamar esta função diretamente.
 *
 * @param[in] samples Amostras na ordem CH1, CH2, CH3 e CH4.
 * @return @c ESP_OK em caso de sucesso ou @c ESP_ERR_INVALID_ARG se @p samples for nulo.
 */
esp_err_t osc_push_frame(const uint16_t samples[4]);

#ifdef __cplusplus
}
#endif
