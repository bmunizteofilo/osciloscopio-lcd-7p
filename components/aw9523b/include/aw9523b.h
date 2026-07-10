#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AW9523B_I2C_ADDR_DEFAULT 0x58
#define AW9523B_I2C_ADDR_AUTO 0xff
#define AW9523B_I2C_ADDR_MIN 0x58
#define AW9523B_I2C_ADDR_MAX 0x5b
#define AW9523B_PORT0_PIN_MAX 7
#define AW9523B_PORT1_PIN_MAX 7

typedef struct aw9523b_t *aw9523b_handle_t;

typedef enum {
    AW9523B_PORT_0 = 0,
    AW9523B_PORT_1 = 1,
} aw9523b_port_t;

typedef struct {
    driver_i2c_bus_handle_t i2c_bus;
    uint8_t i2c_address;
    uint32_t scl_speed_hz;
} aw9523b_config_t;

/**
 * @brief Inicializa o expansor de IO AW9523B no barramento I2C informado.
 *
 * @param[in] config Configuracao do dispositivo AW9523B.
 * @param[out] out_handle Ponteiro que recebe o handle do driver.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_init(const aw9523b_config_t *config, aw9523b_handle_t *out_handle);

/**
 * @brief Libera os recursos usados pelo driver AW9523B.
 *
 * @param[in] handle Handle retornado por aw9523b_init().
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_deinit(aw9523b_handle_t handle);

/**
 * @brief Configura um pino do AW9523B como entrada ou saida GPIO.
 *
 * @param[in] handle Handle do driver AW9523B.
 * @param[in] port Porta do expansor.
 * @param[in] pin Numero do pino dentro da porta.
 * @param[in] output true para saida, false para entrada.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_set_pin_output(aw9523b_handle_t handle, aw9523b_port_t port, uint8_t pin, bool output);

/**
 * @brief Escreve o nivel logico de um pino configurado como saida.
 *
 * @param[in] handle Handle do driver AW9523B.
 * @param[in] port Porta do expansor.
 * @param[in] pin Numero do pino dentro da porta.
 * @param[in] level true para nivel alto, false para nivel baixo.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_set_pin_level(aw9523b_handle_t handle, aw9523b_port_t port, uint8_t pin, bool level);

/**
 * @brief Le o nivel logico atual de um pino do expansor.
 *
 * @param[in] handle Handle do driver AW9523B.
 * @param[in] port Porta do expansor.
 * @param[in] pin Numero do pino dentro da porta.
 * @param[out] out_level Ponteiro que recebe o nivel lido.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_get_pin_level(aw9523b_handle_t handle, aw9523b_port_t port, uint8_t pin, bool *out_level);

#ifdef __cplusplus
}
#endif
