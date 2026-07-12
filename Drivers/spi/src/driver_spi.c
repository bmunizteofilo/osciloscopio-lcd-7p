#include "driver_spi.h"

#include <stdlib.h>

#include "driver/spi_master.h"
#include "esp_check.h"

static const char *TAG = "driver_spi";

/** @brief Estado alocado para um barramento SPI mestre. */
struct driver_spi_bus_t {
    spi_host_device_t host;
};

/** @brief Estado alocado para um dispositivo SPI escravo. */
struct driver_spi_device_t {
    spi_device_handle_t idf_device;
    uint8_t dummy_byte;
};

/**
 * @brief Inicializa um barramento SPI em modo mestre.
 */
esp_err_t driver_spi_bus_init(const driver_spi_bus_config_t *config, driver_spi_bus_handle_t *out_bus)
{
    ESP_RETURN_ON_FALSE(config != NULL && out_bus != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");

    driver_spi_bus_handle_t bus = calloc(1, sizeof(*bus));
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_NO_MEM, TAG, "sem memoria para barramento");

    const spi_bus_config_t idf_config = {
        .mosi_io_num = driver_gpio_to_number(config->mosi_pin),
        .miso_io_num = driver_gpio_to_number(config->miso_pin),
        .sclk_io_num = driver_gpio_to_number(config->sclk_pin),
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = config->max_transfer_size,
    };
    bus->host = (spi_host_device_t)config->host;
    esp_err_t err = spi_bus_initialize(bus->host, &idf_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        free(bus);
        return err;
    }

    *out_bus = bus;
    return ESP_OK;
}

/**
 * @brief Libera um barramento SPI mestre.
 */
esp_err_t driver_spi_bus_deinit(driver_spi_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    esp_err_t err = spi_bus_free(bus->host);
    if (err == ESP_OK) {
        free(bus);
    }
    return err;
}

/**
 * @brief Adiciona um dispositivo escravo ao barramento SPI.
 */
esp_err_t driver_spi_device_add(driver_spi_bus_handle_t bus,
                                const driver_spi_device_config_t *config,
                                driver_spi_device_handle_t *out_device)
{
    ESP_RETURN_ON_FALSE(bus != NULL && config != NULL && out_device != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");
    ESP_RETURN_ON_FALSE(config->mode <= 3, ESP_ERR_INVALID_ARG, TAG, "modo SPI invalido");

    driver_spi_device_handle_t device = calloc(1, sizeof(*device));
    ESP_RETURN_ON_FALSE(device != NULL, ESP_ERR_NO_MEM, TAG, "sem memoria para dispositivo");

    const spi_device_interface_config_t idf_config = {
        .clock_speed_hz = (int)config->clock_hz,
        .mode = config->mode,
        .spics_io_num = driver_gpio_to_number(config->cs_pin),
        .queue_size = config->queue_size == 0 ? 1 : config->queue_size,
    };
    esp_err_t err = spi_bus_add_device(bus->host, &idf_config, &device->idf_device);
    if (err != ESP_OK) {
        free(device);
        return err;
    }

    device->dummy_byte = config->dummy_byte;
    *out_device = device;
    return ESP_OK;
}

/**
 * @brief Remove um dispositivo SPI do barramento.
 */
esp_err_t driver_spi_device_remove(driver_spi_device_handle_t device)
{
    ESP_RETURN_ON_FALSE(device != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    esp_err_t err = spi_bus_remove_device(device->idf_device);
    if (err == ESP_OK) {
        free(device);
    }
    return err;
}

/**
 * @brief Executa uma transferência SPI full-duplex em modo polling.
 */
esp_err_t driver_spi_transfer(driver_spi_device_handle_t device,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              size_t length)
{
    ESP_RETURN_ON_FALSE(device != NULL && length > 0, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");

    uint8_t *dummy = NULL;
    spi_transaction_t transaction = {
        .length = length * 8U,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    if (tx_data == NULL) {
        dummy = malloc(length);
        ESP_RETURN_ON_FALSE(dummy != NULL, ESP_ERR_NO_MEM, TAG, "sem memoria para dummy");
        for (size_t index = 0; index < length; index++) {
            dummy[index] = device->dummy_byte;
        }
        transaction.tx_buffer = dummy;
    }
    esp_err_t err = spi_device_polling_transmit(device->idf_device, &transaction);
    free(dummy);
    return err;
}

/**
 * @brief Envia bytes ao escravo em uma transação polling.
 */
esp_err_t driver_spi_write(driver_spi_device_handle_t device, const uint8_t *data, size_t length)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "dados invalidos");
    return driver_spi_transfer(device, data, NULL, length);
}

/**
 * @brief Lê bytes do escravo gerando clocks com o byte dummy configurado.
 */
esp_err_t driver_spi_read(driver_spi_device_handle_t device, uint8_t *data, size_t length)
{
    ESP_RETURN_ON_FALSE(device != NULL && data != NULL && length > 0, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");

    return driver_spi_transfer(device, NULL, data, length);
}
