#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "aw9523b.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WT32S3_LCD_H_RES 800
#define WT32S3_LCD_V_RES 480
#define WT32S3_LCD_BITS_PER_PIXEL 16

typedef struct wt32s3_lcd_t *wt32s3_lcd_handle_t;

/**
 * @brief Inicializa o painel RGB da placa WT32S3-07S.
 *
 * @param[in] io_expander Handle do expansor AW9523B usado no reset do LCD.
 * @param[out] out_handle Ponteiro que recebe o handle do driver de LCD.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t wt32s3_lcd_init(aw9523b_handle_t io_expander, wt32s3_lcd_handle_t *out_handle);

/**
 * @brief Libera os recursos usados pelo driver do LCD.
 *
 * @param[in] handle Handle retornado por wt32s3_lcd_init().
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t wt32s3_lcd_deinit(wt32s3_lcd_handle_t handle);

/**
 * @brief Ajusta o brilho do backlight do display.
 *
 * @param[in] handle Handle do driver de LCD.
 * @param[in] percent Brilho em porcentagem, limitado de 0 a 100.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t wt32s3_lcd_set_backlight(wt32s3_lcd_handle_t handle, uint8_t percent);

/**
 * @brief Retorna os dois framebuffers internos alocados pelo driver RGB.
 *
 * @param[in] handle Handle do driver de LCD.
 * @param[out] framebuffer_a Ponteiro que recebe o primeiro framebuffer.
 * @param[out] framebuffer_b Ponteiro que recebe o segundo framebuffer.
 * @return ESP_OK em caso de sucesso ou codigo de erro do driver RGB.
 */
esp_err_t wt32s3_lcd_get_frame_buffers(wt32s3_lcd_handle_t handle, void **framebuffer_a, void **framebuffer_b);

/**
 * @brief Retorna o handle nativo do painel RGB do ESP-IDF.
 *
 * @param[in] handle Handle do driver de LCD.
 * @return Handle do painel ou NULL se o argumento for invalido.
 */
esp_lcd_panel_handle_t wt32s3_lcd_get_panel(wt32s3_lcd_handle_t handle);

#ifdef __cplusplus
}
#endif
