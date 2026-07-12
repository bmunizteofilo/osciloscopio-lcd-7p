#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DRIVER_BLUETOOTH_MAX_DEVICES 12
#define DRIVER_BLUETOOTH_NAME_MAX_LENGTH 29

/** @brief Dispositivo Bluetooth Low Energy descoberto durante a varredura. */
typedef struct {
    char name[DRIVER_BLUETOOTH_NAME_MAX_LENGTH + 1];
    char address[18];
    int8_t rssi;
} driver_bluetooth_device_t;

/** @brief Evento assíncrono emitido pelo driver Bluetooth. */
typedef enum {
    DRIVER_BLUETOOTH_EVENT_DEVICE_FOUND,
    DRIVER_BLUETOOTH_EVENT_SCAN_COMPLETE,
} driver_bluetooth_event_t;

/** @brief Callback chamado por eventos do driver Bluetooth. */
typedef void (*driver_bluetooth_event_cb_t)(driver_bluetooth_event_t event, void *context);

/**
 * @brief Inicializa a pilha NimBLE para operação apenas como scanner BLE.
 *
 * @param[in] callback Callback para eventos assíncronos.
 * @param[in] context Contexto entregue ao callback.
 * @return @c ESP_OK em caso de sucesso.
 */
esp_err_t driver_bluetooth_init(driver_bluetooth_event_cb_t callback, void *context);

/**
 * @brief Para a varredura e libera a pilha NimBLE e o controlador BLE.
 *
 * @return @c ESP_OK em caso de sucesso ou erro durante a finalização.
 */
esp_err_t driver_bluetooth_deinit(void);

/**
 * @brief Liga ou desliga a busca por dispositivos Bluetooth Low Energy.
 *
 * @param[in] enabled @c true para iniciar busca ou @c false para cancelá-la.
 * @return @c ESP_OK em caso de sucesso.
 */
esp_err_t driver_bluetooth_set_enabled(bool enabled);

/**
 * @brief Copia os dispositivos descobertos desde a última ativação.
 *
 * @param[out] devices Vetor de destino.
 * @param[in] capacity Capacidade máxima do vetor.
 * @param[out] out_count Quantidade de dispositivos copiados.
 * @return @c ESP_OK em caso de sucesso.
 */
esp_err_t driver_bluetooth_get_devices(driver_bluetooth_device_t *devices, uint16_t capacity, uint16_t *out_count);

#ifdef __cplusplus
}
#endif
