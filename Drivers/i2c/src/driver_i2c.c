#include "driver_i2c.h"
#include <stdlib.h>
#include "driver/i2c_master.h"
#include "esp_check.h"

static const char *TAG = "driver_i2c";

struct driver_i2c_bus_t {
    i2c_master_bus_handle_t idf_bus;
};

struct driver_i2c_device_t {
    i2c_master_dev_handle_t idf_device;
};

/**
 * @brief Inicializa um barramento I2C mestre.
 *
 * @param[in] config Configuracao do barramento I2C.
 * @param[out] out_bus Ponteiro que recebe o handle do barramento.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_bus_init(const driver_i2c_bus_config_t *config, driver_i2c_bus_handle_t *out_bus)
{
    ESP_RETURN_ON_FALSE(config != NULL && out_bus != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");

    driver_i2c_bus_handle_t bus = calloc(1, sizeof(*bus));
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_NO_MEM, TAG, "sem memoria para barramento");

    const i2c_master_bus_config_t idf_config = {
        .i2c_port = config->port,
        .sda_io_num = (gpio_num_t)driver_gpio_to_number(config->sda_pin),
        .scl_io_num = (gpio_num_t)driver_gpio_to_number(config->scl_pin),
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = config->glitch_ignore_count,
        .flags.enable_internal_pullup = config->enable_internal_pullup,
    };

    esp_err_t err = i2c_new_master_bus(&idf_config, &bus->idf_bus);
    if (err != ESP_OK) {
        free(bus);
        return err;
    }

    *out_bus = bus;
    return ESP_OK;
}

/**
 * @brief Libera um barramento I2C mestre.
 *
 * @param[in] bus Handle do barramento criado por driver_i2c_bus_init().
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_bus_deinit(driver_i2c_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    esp_err_t err = i2c_del_master_bus(bus->idf_bus);
    free(bus);
    return err;
}

/**
 * @brief Adiciona um dispositivo ao barramento I2C.
 *
 * @param[in] bus Handle do barramento I2C.
 * @param[in] config Configuracao do dispositivo I2C.
 * @param[out] out_device Ponteiro que recebe o handle do dispositivo.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_device_add(driver_i2c_bus_handle_t bus, const driver_i2c_device_config_t *config, driver_i2c_device_handle_t *out_device)
{
    ESP_RETURN_ON_FALSE(bus != NULL && config != NULL && out_device != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");

    driver_i2c_device_handle_t device = calloc(1, sizeof(*device));
    ESP_RETURN_ON_FALSE(device != NULL, ESP_ERR_NO_MEM, TAG, "sem memoria para dispositivo");

    const i2c_device_config_t idf_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->address,
        .scl_speed_hz = config->scl_speed_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(bus->idf_bus, &idf_config, &device->idf_device);
    if (err != ESP_OK) {
        free(device);
        return err;
    }

    *out_device = device;
    return ESP_OK;
}

/**
 * @brief Verifica se um endereco responde no barramento I2C.
 *
 * @param[in] bus Handle do barramento I2C.
 * @param[in] address Endereco I2C de 7 bits.
 * @param[in] timeout_ms Tempo maximo da tentativa em milissegundos.
 * @return ESP_OK quando ha ACK no endereco informado.
 */
esp_err_t driver_i2c_probe(driver_i2c_bus_handle_t bus, uint8_t address, int timeout_ms)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    return i2c_master_probe(bus->idf_bus, address, timeout_ms);
}

/**
 * @brief Remove um dispositivo do barramento I2C.
 *
 * @param[in] device Handle do dispositivo I2C.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_device_remove(driver_i2c_device_handle_t device)
{
    ESP_RETURN_ON_FALSE(device != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    esp_err_t err = i2c_master_bus_rm_device(device->idf_device);
    free(device);
    return err;
}

/**
 * @brief Envia bytes para um dispositivo I2C.
 *
 * @param[in] device Handle do dispositivo I2C.
 * @param[in] data Ponteiro para os bytes a enviar.
 * @param[in] length Quantidade de bytes a enviar.
 * @param[in] timeout_ms Tempo maximo da transferencia em milissegundos.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t driver_i2c_write(driver_i2c_device_handle_t device, const uint8_t *data, size_t length, int timeout_ms)
{
    ESP_RETURN_ON_FALSE(device != NULL && data != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");
    return i2c_master_transmit(device->idf_device, data, length, timeout_ms);
}

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
                             int timeout_ms)
{
    ESP_RETURN_ON_FALSE(device != NULL && write_data != NULL && read_data != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");
    return i2c_master_transmit_receive(device->idf_device, write_data, write_length, read_data, read_length, timeout_ms);
}
