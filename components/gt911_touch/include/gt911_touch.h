#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver_i2c.h"
#include "aw9523b.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GT911_TOUCH_MAX_POINTS 5

typedef struct gt911_touch_t *gt911_touch_handle_t;

typedef struct {
    driver_i2c_bus_handle_t i2c_bus;
    aw9523b_handle_t reset_io;
    uint32_t scl_speed_hz;
    uint16_t x_max;
    uint16_t y_max;
} gt911_touch_config_t;

typedef struct {
    bool touched;
    uint8_t points;
    uint16_t x[GT911_TOUCH_MAX_POINTS];
    uint16_t y[GT911_TOUCH_MAX_POINTS];
    uint16_t size[GT911_TOUCH_MAX_POINTS];
} gt911_touch_data_t;

/**
 * @brief Inicializa o controlador de toque GT911.
 *
 * @param[in] config Configuracao do barramento I2C e do reset via AW9523B.
 * @param[out] out_handle Ponteiro que recebe o handle do driver.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t gt911_touch_init(const gt911_touch_config_t *config, gt911_touch_handle_t *out_handle);

/**
 * @brief Libera os recursos usados pelo driver GT911.
 *
 * @param[in] handle Handle retornado por gt911_touch_init().
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t gt911_touch_deinit(gt911_touch_handle_t handle);

/**
 * @brief Le o estado atual do toque por polling.
 *
 * @param[in] handle Handle do driver GT911.
 * @param[out] out_data Ponteiro que recebe os pontos de toque.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t gt911_touch_read(gt911_touch_handle_t handle, gt911_touch_data_t *out_data);

/**
 * @brief Retorna o endereco I2C detectado para o GT911.
 *
 * @param[in] handle Handle do driver GT911.
 * @return Endereco I2C de 7 bits ou zero se o handle for invalido.
 */
uint8_t gt911_touch_get_address(gt911_touch_handle_t handle);

#ifdef __cplusplus
}
#endif
