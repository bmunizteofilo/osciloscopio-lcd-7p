#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver_gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Handle opaco de um barramento SPI mestre. */
typedef struct driver_spi_bus_t *driver_spi_bus_handle_t;
/** @brief Handle opaco de um dispositivo conectado ao barramento SPI. */
typedef struct driver_spi_device_t *driver_spi_device_handle_t;

/** @brief Configuração física do barramento SPI mestre. */
typedef struct {
    int host;
    driver_gpio_num_t mosi_pin;
    driver_gpio_num_t miso_pin;
    driver_gpio_num_t sclk_pin;
    size_t max_transfer_size;
} driver_spi_bus_config_t;

/** @brief Configuração de um dispositivo SPI escravo. */
typedef struct {
    driver_gpio_num_t cs_pin;
    uint32_t clock_hz;
    uint8_t mode;
    uint8_t queue_size;
    uint8_t dummy_byte;
} driver_spi_device_config_t;

/**
 * @brief Inicializa um barramento SPI em modo mestre.
 *
 * @param[in] config Configuração física do barramento.
 * @param[out] out_bus Ponteiro que recebe o handle criado.
 * @return ESP_OK em sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_spi_bus_init(const driver_spi_bus_config_t *config, driver_spi_bus_handle_t *out_bus);

/**
 * @brief Libera um barramento SPI mestre.
 *
 * Todos os dispositivos devem ser removidos antes desta chamada.
 *
 * @param[in] bus Handle do barramento.
 * @return ESP_OK em sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_spi_bus_deinit(driver_spi_bus_handle_t bus);

/**
 * @brief Adiciona um dispositivo escravo ao barramento SPI.
 *
 * @param[in] bus Handle do barramento SPI.
 * @param[in] config Configuração do dispositivo.
 * @param[out] out_device Ponteiro que recebe o handle criado.
 * @return ESP_OK em sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_spi_device_add(driver_spi_bus_handle_t bus,
                                const driver_spi_device_config_t *config,
                                driver_spi_device_handle_t *out_device);

/**
 * @brief Remove um dispositivo SPI do barramento.
 *
 * @param[in] device Handle do dispositivo.
 * @return ESP_OK em sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_spi_device_remove(driver_spi_device_handle_t device);

/**
 * @brief Envia bytes ao escravo em uma transação polling.
 *
 * @param[in] device Handle do dispositivo.
 * @param[in] data Bytes a enviar, normalmente um comando ou payload.
 * @param[in] length Quantidade de bytes a enviar.
 * @return ESP_OK em sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_spi_write(driver_spi_device_handle_t device, const uint8_t *data, size_t length);

/**
 * @brief Lê bytes do escravo gerando clocks com o byte dummy configurado.
 *
 * O mestre aciona CS e transmite o byte dummy repetidamente; os bytes recebidos
 * em MISO são entregues em @p data.
 *
 * @param[in] device Handle do dispositivo.
 * @param[out] data Buffer que recebe os bytes lidos.
 * @param[in] length Quantidade de bytes a ler.
 * @return ESP_OK em sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_spi_read(driver_spi_device_handle_t device, uint8_t *data, size_t length);

/**
 * @brief Executa uma transferência SPI full-duplex em modo polling.
 *
 * @param[in] device Handle do dispositivo.
 * @param[in] tx_data Bytes enviados em MOSI; pode ser NULL para enviar dummy.
 * @param[out] rx_data Buffer de MISO; pode ser NULL quando a resposta não importa.
 * @param[in] length Quantidade de bytes transferidos.
 * @return ESP_OK em sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_spi_transfer(driver_spi_device_handle_t device,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              size_t length);

#ifdef __cplusplus
}
#endif
