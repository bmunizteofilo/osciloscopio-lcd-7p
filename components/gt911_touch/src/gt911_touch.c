#include "gt911_touch.h"
#include <string.h>
#include <stdlib.h>
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GT911_ADDR_PRIMARY 0x5d
#define GT911_ADDR_SECONDARY 0x14
#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_STATUS 0x814e
#define GT911_REG_POINT1 0x814f
#define GT911_STATUS_BUFFER_READY 0x80
#define GT911_STATUS_POINT_MASK 0x0f
#define GT911_TIMEOUT_MS 100
#define GT911_RST_PORT AW9523B_PORT_1
#define GT911_RST_PIN 1

static const char *TAG = "gt911";

struct gt911_touch_t {
    driver_i2c_bus_handle_t i2c_bus;
    driver_i2c_device_handle_t i2c_dev;
    aw9523b_handle_t reset_io;
    uint8_t address;
    uint16_t x_max;
    uint16_t y_max;
};

/**
 * @brief Escreve bytes em um registrador de 16 bits do GT911.
 *
 * @param[in] handle Handle do driver GT911.
 * @param[in] reg Endereco do registrador.
 * @param[in] data Ponteiro para os dados.
 * @param[in] length Quantidade de bytes a escrever.
 * @return ESP_OK em caso de sucesso ou erro do barramento I2C.
 */
static esp_err_t gt911_write_reg(gt911_touch_handle_t handle, uint16_t reg, const uint8_t *data, size_t length)
{
    uint8_t buffer[2 + 8] = {0};
    ESP_RETURN_ON_FALSE(length <= 8, ESP_ERR_INVALID_ARG, TAG, "escrita muito longa");
    buffer[0] = (uint8_t)(reg >> 8);
    buffer[1] = (uint8_t)(reg & 0xff);
    memcpy(&buffer[2], data, length);
    return driver_i2c_write(handle->i2c_dev, buffer, length + 2, GT911_TIMEOUT_MS);
}

/**
 * @brief Le bytes de um registrador de 16 bits do GT911.
 *
 * @param[in] handle Handle do driver GT911.
 * @param[in] reg Endereco do registrador.
 * @param[out] data Ponteiro que recebe os dados.
 * @param[in] length Quantidade de bytes a ler.
 * @return ESP_OK em caso de sucesso ou erro do barramento I2C.
 */
static esp_err_t gt911_read_reg(gt911_touch_handle_t handle, uint16_t reg, uint8_t *data, size_t length)
{
    uint8_t reg_buf[] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xff),
    };
    return driver_i2c_write_read(handle->i2c_dev, reg_buf, sizeof(reg_buf), data, length, GT911_TIMEOUT_MS);
}

/**
 * @brief Executa reset fisico do GT911 pelo AW9523B.
 *
 * @param[in] reset_io Handle do expansor AW9523B.
 * @return ESP_OK em caso de sucesso ou erro do expansor.
 */
static esp_err_t gt911_reset(aw9523b_handle_t reset_io)
{
    ESP_RETURN_ON_ERROR(aw9523b_set_pin_output(reset_io, GT911_RST_PORT, GT911_RST_PIN, true), TAG, "falha ao configurar reset");
    ESP_RETURN_ON_ERROR(aw9523b_set_pin_level(reset_io, GT911_RST_PORT, GT911_RST_PIN, false), TAG, "falha ao baixar reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(aw9523b_set_pin_level(reset_io, GT911_RST_PORT, GT911_RST_PIN, true), TAG, "falha ao liberar reset");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

/**
 * @brief Tenta adicionar e validar o GT911 em um endereco I2C.
 *
 * @param[in,out] handle Handle parcialmente inicializado do driver.
 * @param[in] address Endereco I2C de 7 bits a testar.
 * @param[in] scl_speed_hz Frequencia do barramento para o dispositivo.
 * @return ESP_OK quando o GT911 responde no endereco informado.
 */
static esp_err_t gt911_try_address(gt911_touch_handle_t handle, uint8_t address, uint32_t scl_speed_hz)
{
    const driver_i2c_device_config_t dev_config = {
        .address = address,
        .scl_speed_hz = scl_speed_hz,
    };

    esp_err_t err = driver_i2c_device_add(handle->i2c_bus, &dev_config, &handle->i2c_dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t product_id[4] = {0};
    err = gt911_read_reg(handle, GT911_REG_PRODUCT_ID, product_id, sizeof(product_id));
    if (err != ESP_OK || product_id[0] != '9' || product_id[1] != '1') {
        driver_i2c_device_remove(handle->i2c_dev);
        handle->i2c_dev = NULL;
        return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    }

    handle->address = address;
    return ESP_OK;
}

/**
 * @brief Inicializa o controlador de toque GT911.
 *
 * @param[in] config Configuracao do barramento I2C e do reset via AW9523B.
 * @param[out] out_handle Ponteiro que recebe o handle do driver.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t gt911_touch_init(const gt911_touch_config_t *config, gt911_touch_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(config != NULL && out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");
    ESP_RETURN_ON_FALSE(config->i2c_bus != NULL && config->reset_io != NULL, ESP_ERR_INVALID_ARG, TAG, "dependencia invalida");

    gt911_touch_handle_t handle = calloc(1, sizeof(*handle));
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_NO_MEM, TAG, "sem memoria para handle");

    handle->i2c_bus = config->i2c_bus;
    handle->reset_io = config->reset_io;
    handle->x_max = config->x_max;
    handle->y_max = config->y_max;

    esp_err_t ret = gt911_reset(config->reset_io);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "falha no reset do GT911");

    ret = gt911_try_address(handle, GT911_ADDR_PRIMARY, config->scl_speed_hz);
    if (ret != ESP_OK) {
        ret = gt911_try_address(handle, GT911_ADDR_SECONDARY, config->scl_speed_hz);
    }
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "GT911 nao encontrado");

    *out_handle = handle;
    return ESP_OK;

fail:
    if (handle->i2c_dev != NULL) {
        driver_i2c_device_remove(handle->i2c_dev);
    }
    free(handle);
    return ret;
}

/**
 * @brief Libera os recursos usados pelo driver GT911.
 *
 * @param[in] handle Handle retornado por gt911_touch_init().
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t gt911_touch_deinit(gt911_touch_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    esp_err_t err = driver_i2c_device_remove(handle->i2c_dev);
    free(handle);
    return err;
}

/**
 * @brief Le o estado atual do toque por polling.
 *
 * @param[in] handle Handle do driver GT911.
 * @param[out] out_data Ponteiro que recebe os pontos de toque.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t gt911_touch_read(gt911_touch_handle_t handle, gt911_touch_data_t *out_data)
{
    ESP_RETURN_ON_FALSE(handle != NULL && out_data != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");
    memset(out_data, 0, sizeof(*out_data));

    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(gt911_read_reg(handle, GT911_REG_STATUS, &status, sizeof(status)), TAG, "falha ao ler status");
    if ((status & GT911_STATUS_BUFFER_READY) == 0) {
        return ESP_OK;
    }

    uint8_t points = status & GT911_STATUS_POINT_MASK;
    if (points > GT911_TOUCH_MAX_POINTS) {
        points = GT911_TOUCH_MAX_POINTS;
    }

    if (points > 0) {
        uint8_t point_data[GT911_TOUCH_MAX_POINTS * 8] = {0};
        ESP_RETURN_ON_ERROR(gt911_read_reg(handle, GT911_REG_POINT1, point_data, points * 8), TAG, "falha ao ler pontos");
        out_data->touched = true;
        out_data->points = points;

        for (uint8_t i = 0; i < points; i++) {
            const uint8_t *p = &point_data[i * 8];
            out_data->x[i] = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
            out_data->y[i] = (uint16_t)p[3] | ((uint16_t)p[4] << 8);
            out_data->size[i] = (uint16_t)p[5] | ((uint16_t)p[6] << 8);
            if (out_data->x[i] >= handle->x_max) {
                out_data->x[i] = handle->x_max - 1;
            }
            if (out_data->y[i] >= handle->y_max) {
                out_data->y[i] = handle->y_max - 1;
            }
        }
    }

    uint8_t clear_status = 0;
    return gt911_write_reg(handle, GT911_REG_STATUS, &clear_status, sizeof(clear_status));
}

/**
 * @brief Retorna o endereco I2C detectado para o GT911.
 *
 * @param[in] handle Handle do driver GT911.
 * @return Endereco I2C de 7 bits ou zero se o handle for invalido.
 */
uint8_t gt911_touch_get_address(gt911_touch_handle_t handle)
{
    if (handle == NULL) {
        return 0;
    }
    return handle->address;
}
