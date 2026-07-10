#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct driver_i2c_bus_t *driver_i2c_bus_handle_t;
typedef struct driver_i2c_device_t *driver_i2c_device_handle_t;

typedef struct {
    int port;
    driver_gpio_num_t sda_pin;
    driver_gpio_num_t scl_pin;
    uint8_t glitch_ignore_count;
    bool enable_internal_pullup;
} driver_i2c_bus_config_t;

typedef struct {
    uint8_t address;
    uint32_t scl_speed_hz;
} driver_i2c_device_config_t;

/**
 * @brief Inicializa um barramento I2C mestre.
 *
 * @param[in] config Configuracao do barramento I2C.
 * @param[out] out_bus Ponteiro que recebe o handle do barramento.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_bus_init(const driver_i2c_bus_config_t *config, driver_i2c_bus_handle_t *out_bus);

/**
 * @brief Libera um barramento I2C mestre.
 *
 * @param[in] bus Handle do barramento criado por driver_i2c_bus_init().
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_bus_deinit(driver_i2c_bus_handle_t bus);

/**
 * @brief Adiciona um dispositivo ao barramento I2C.
 *
 * @param[in] bus Handle do barramento I2C.
 * @param[in] config Configuracao do dispositivo I2C.
 * @param[out] out_device Ponteiro que recebe o handle do dispositivo.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_device_add(driver_i2c_bus_handle_t bus, const driver_i2c_device_config_t *config, driver_i2c_device_handle_t *out_device);

/**
 * @brief Verifica se um endereco responde no barramento I2C.
 *
 * @param[in] bus Handle do barramento I2C.
 * @param[in] address Endereco I2C de 7 bits.
 * @param[in] timeout_ms Tempo maximo da tentativa em milissegundos.
 * @return ESP_OK quando ha ACK no endereco informado.
 */
esp_err_t driver_i2c_probe(driver_i2c_bus_handle_t bus, uint8_t address, int timeout_ms);

/**
 * @brief Remove um dispositivo do barramento I2C.
 *
 * @param[in] device Handle do dispositivo I2C.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_device_remove(driver_i2c_device_handle_t device);

/**
 * @brief Envia bytes para um dispositivo I2C.
 *
 * @param[in] device Handle do dispositivo I2C.
 * @param[in] data Ponteiro para os bytes a enviar.
 * @param[in] length Quantidade de bytes a enviar.
 * @param[in] timeout_ms Tempo maximo da transferencia em milissegundos.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_write(driver_i2c_device_handle_t device, const uint8_t *data, size_t length, int timeout_ms);

/**
 * @brief Executa escrita seguida de leitura em um dispositivo I2C.
 *
 * @param[in] device Handle do dispositivo I2C.
 * @param[in] write_data Ponteiro para os bytes enviados antes da leitura.
 * @param[in] write_length Quantidade de bytes enviados antes da leitura.
 * @param[out] read_data Ponteiro que recebe os bytes lidos.
 * @param[in] read_length Quantidade de bytes a ler.
 * @param[in] timeout_ms Tempo maximo da transferencia em milissegundos.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_write_read(driver_i2c_device_handle_t device,
                             const uint8_t *write_data,
                             size_t write_length,
                             uint8_t *read_data,
                             size_t read_length,
                             int timeout_ms);

#ifdef __cplusplus
}
#endif
