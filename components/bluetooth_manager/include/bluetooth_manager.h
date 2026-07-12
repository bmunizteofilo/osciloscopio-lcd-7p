#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver_bluetooth.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Estado Bluetooth público consumido pela interface LVGL. */
typedef struct {
    bool enabled;
    bool scanning;
    uint16_t device_count;
    driver_bluetooth_device_t devices[DRIVER_BLUETOOTH_MAX_DEVICES];
} bluetooth_manager_status_t;

/**
 * @brief Cria a task gerenciadora de Bluetooth fixada no core 0.
 *
 * @return @c ESP_OK em caso de sucesso.
 */
esp_err_t bluetooth_manager_start(void);

/**
 * @brief Solicita ligar ou desligar a varredura Bluetooth de forma assíncrona.
 *
 * @param[in] enabled Novo estado solicitado.
 * @return @c ESP_OK ao enfileirar o comando.
 */
esp_err_t bluetooth_manager_set_enabled(bool enabled);

/**
 * @brief Copia o estado e os dispositivos Bluetooth encontrados.
 *
 * @param[out] out_status Estrutura que recebe o estado atual.
 * @return @c ESP_OK em caso de sucesso.
 */
esp_err_t bluetooth_manager_get_status(bluetooth_manager_status_t *out_status);

#ifdef __cplusplus
}
#endif
