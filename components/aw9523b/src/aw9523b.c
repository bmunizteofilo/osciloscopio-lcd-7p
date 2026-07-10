#include "aw9523b.h"
#include <stdlib.h>
#include "esp_check.h"
#include "esp_log.h"

#define AW9523B_REG_INPUT_P0 0x00
#define AW9523B_REG_OUTPUT_P0 0x02
#define AW9523B_REG_CONFIG_P0 0x04
#define AW9523B_REG_LED_MODE_P0 0x12
#define AW9523B_REG_GLOBAL_CONTROL 0x11
#define AW9523B_TIMEOUT_MS 100

static const char *TAG = "aw9523b";

struct aw9523b_t {
    driver_i2c_device_handle_t i2c_dev;
    uint8_t i2c_address;
    uint8_t output_cache[2];
    uint8_t config_cache[2];
};

/**
 * @brief Valida se uma porta e pino pertencem ao AW9523B.
 *
 * @param[in] port Porta do expansor.
 * @param[in] pin Numero do pino dentro da porta.
 * @return ESP_OK quando o par porta/pino e valido.
 */
static esp_err_t aw9523b_validate_pin(aw9523b_port_t port, uint8_t pin)
{
    if (port == AW9523B_PORT_0 && pin <= AW9523B_PORT0_PIN_MAX) {
        return ESP_OK;
    }
    if (port == AW9523B_PORT_1 && pin <= AW9523B_PORT1_PIN_MAX) {
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

/**
 * @brief Escreve um registrador de 8 bits do AW9523B.
 *
 * @param[in] handle Handle do driver AW9523B.
 * @param[in] reg Endereco do registrador.
 * @param[in] value Valor a ser escrito.
 * @return ESP_OK em caso de sucesso ou erro do barramento I2C.
 */
static esp_err_t aw9523b_write_reg(aw9523b_handle_t handle, uint8_t reg, uint8_t value)
{
    uint8_t buffer[] = {reg, value};
    return driver_i2c_write(handle->i2c_dev, buffer, sizeof(buffer), AW9523B_TIMEOUT_MS);
}

/**
 * @brief Le um registrador de 8 bits do AW9523B.
 *
 * @param[in] handle Handle do driver AW9523B.
 * @param[in] reg Endereco do registrador.
 * @param[out] value Ponteiro que recebe o valor lido.
 * @return ESP_OK em caso de sucesso ou erro do barramento I2C.
 */
static esp_err_t aw9523b_read_reg(aw9523b_handle_t handle, uint8_t reg, uint8_t *value)
{
    return driver_i2c_write_read(handle->i2c_dev, &reg, sizeof(reg), value, sizeof(*value), AW9523B_TIMEOUT_MS);
}

/**
 * @brief Localiza o endereco I2C ativo do AW9523B.
 *
 * @param[in] config Configuracao do dispositivo AW9523B.
 * @param[out] out_address Ponteiro que recebe o endereco encontrado.
 * @return ESP_OK quando algum endereco responde no barramento.
 */
static esp_err_t aw9523b_detect_address(const aw9523b_config_t *config, uint8_t *out_address)
{
    ESP_RETURN_ON_FALSE(config != NULL && out_address != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");

    if (config->i2c_address != AW9523B_I2C_ADDR_AUTO) {
        esp_err_t err = driver_i2c_probe(config->i2c_bus, config->i2c_address, AW9523B_TIMEOUT_MS);
        if (err == ESP_OK) {
            *out_address = config->i2c_address;
        }
        return err;
    }

    for (uint8_t address = AW9523B_I2C_ADDR_MIN; address <= AW9523B_I2C_ADDR_MAX; address++) {
        if (driver_i2c_probe(config->i2c_bus, address, AW9523B_TIMEOUT_MS) == ESP_OK) {
            *out_address = address;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief Inicializa o expansor de IO AW9523B no barramento I2C informado.
 *
 * @param[in] config Configuracao do dispositivo AW9523B.
 * @param[out] out_handle Ponteiro que recebe o handle do driver.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_init(const aw9523b_config_t *config, aw9523b_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(config != NULL && out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");
    ESP_RETURN_ON_FALSE(config->i2c_bus != NULL, ESP_ERR_INVALID_ARG, TAG, "barramento I2C invalido");

    aw9523b_handle_t handle = calloc(1, sizeof(*handle));
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_NO_MEM, TAG, "sem memoria para handle");

    uint8_t detected_address = 0;
    esp_err_t ret = aw9523b_detect_address(config, &detected_address);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }

    const driver_i2c_device_config_t dev_config = {
        .address = detected_address,
        .scl_speed_hz = config->scl_speed_hz,
    };

    ret = driver_i2c_device_add(config->i2c_bus, &dev_config, &handle->i2c_dev);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }
    handle->i2c_address = detected_address;
    ESP_LOGI(TAG, "AW9523B detectado no endereco I2C 0x%02x", detected_address);

    ESP_GOTO_ON_ERROR(aw9523b_write_reg(handle, AW9523B_REG_GLOBAL_CONTROL, 0x00), fail, TAG, "falha ao configurar controle global");
    ESP_GOTO_ON_ERROR(aw9523b_write_reg(handle, AW9523B_REG_LED_MODE_P0, 0x00), fail, TAG, "falha ao colocar P0 em GPIO");
    ESP_GOTO_ON_ERROR(aw9523b_write_reg(handle, AW9523B_REG_LED_MODE_P0 + 1, 0x00), fail, TAG, "falha ao colocar P1 em GPIO");

    handle->output_cache[0] = 0x00;
    handle->output_cache[1] = 0x00;
    handle->config_cache[0] = 0xff;
    handle->config_cache[1] = 0xff;
    ESP_GOTO_ON_ERROR(aw9523b_write_reg(handle, AW9523B_REG_OUTPUT_P0, handle->output_cache[0]), fail, TAG, "falha ao limpar P0");
    ESP_GOTO_ON_ERROR(aw9523b_write_reg(handle, AW9523B_REG_OUTPUT_P0 + 1, handle->output_cache[1]), fail, TAG, "falha ao limpar P1");
    ESP_GOTO_ON_ERROR(aw9523b_write_reg(handle, AW9523B_REG_CONFIG_P0, handle->config_cache[0]), fail, TAG, "falha ao configurar P0");
    ESP_GOTO_ON_ERROR(aw9523b_write_reg(handle, AW9523B_REG_CONFIG_P0 + 1, handle->config_cache[1]), fail, TAG, "falha ao configurar P1");

    *out_handle = handle;
    return ESP_OK;

fail:
    driver_i2c_device_remove(handle->i2c_dev);
    free(handle);
    return ret;
}

/**
 * @brief Libera os recursos usados pelo driver AW9523B.
 *
 * @param[in] handle Handle retornado por aw9523b_init().
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_deinit(aw9523b_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    esp_err_t err = driver_i2c_device_remove(handle->i2c_dev);
    free(handle);
    return err;
}

/**
 * @brief Configura um pino do AW9523B como entrada ou saida GPIO.
 *
 * @param[in] handle Handle do driver AW9523B.
 * @param[in] port Porta do expansor.
 * @param[in] pin Numero do pino dentro da porta.
 * @param[in] output true para saida, false para entrada.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_set_pin_output(aw9523b_handle_t handle, aw9523b_port_t port, uint8_t pin, bool output)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    ESP_RETURN_ON_ERROR(aw9523b_validate_pin(port, pin), TAG, "pino invalido");

    uint8_t index = (uint8_t)port;
    uint8_t mask = (uint8_t)(1U << pin);
    if (output) {
        handle->config_cache[index] &= (uint8_t)~mask;
    } else {
        handle->config_cache[index] |= mask;
    }

    return aw9523b_write_reg(handle, (uint8_t)(AW9523B_REG_CONFIG_P0 + index), handle->config_cache[index]);
}

/**
 * @brief Escreve o nivel logico de um pino configurado como saida.
 *
 * @param[in] handle Handle do driver AW9523B.
 * @param[in] port Porta do expansor.
 * @param[in] pin Numero do pino dentro da porta.
 * @param[in] level true para nivel alto, false para nivel baixo.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_set_pin_level(aw9523b_handle_t handle, aw9523b_port_t port, uint8_t pin, bool level)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    ESP_RETURN_ON_ERROR(aw9523b_validate_pin(port, pin), TAG, "pino invalido");

    uint8_t index = (uint8_t)port;
    uint8_t mask = (uint8_t)(1U << pin);
    if (level) {
        handle->output_cache[index] |= mask;
    } else {
        handle->output_cache[index] &= (uint8_t)~mask;
    }

    return aw9523b_write_reg(handle, (uint8_t)(AW9523B_REG_OUTPUT_P0 + index), handle->output_cache[index]);
}

/**
 * @brief Le o nivel logico atual de um pino do expansor.
 *
 * @param[in] handle Handle do driver AW9523B.
 * @param[in] port Porta do expansor.
 * @param[in] pin Numero do pino dentro da porta.
 * @param[out] out_level Ponteiro que recebe o nivel lido.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t aw9523b_get_pin_level(aw9523b_handle_t handle, aw9523b_port_t port, uint8_t pin, bool *out_level)
{
    ESP_RETURN_ON_FALSE(handle != NULL && out_level != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");
    ESP_RETURN_ON_ERROR(aw9523b_validate_pin(port, pin), TAG, "pino invalido");

    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(aw9523b_read_reg(handle, (uint8_t)(AW9523B_REG_INPUT_P0 + (uint8_t)port), &value), TAG, "falha ao ler entrada");
    *out_level = (value & (uint8_t)(1U << pin)) != 0;
    return ESP_OK;
}
