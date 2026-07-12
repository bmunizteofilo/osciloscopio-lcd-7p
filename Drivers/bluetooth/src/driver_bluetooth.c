#include "driver_bluetooth.h"

#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_nimble_hci.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

/** @brief Estado privado da pilha BLE. */
typedef struct {
    driver_bluetooth_event_cb_t callback;
    void *callback_context;
    driver_bluetooth_device_t devices[DRIVER_BLUETOOTH_MAX_DEVICES];
    uint16_t device_count;
    uint8_t address_type;
    SemaphoreHandle_t host_stopped;
    SemaphoreHandle_t scan_finished;
    bool initialized;
    bool ready;
    bool scanning;
} driver_bluetooth_context_t;

/** @brief Estado persistente do driver Bluetooth. */
static driver_bluetooth_context_t s_bluetooth;

/** @brief Informa o gerenciador que a pilha NimBLE está pronta. */
static void driver_bluetooth_on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_bluetooth.address_type);
    s_bluetooth.ready = true;
}

/** @brief Executa a pilha NimBLE em sua task própria. */
static void driver_bluetooth_host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    xSemaphoreGive(s_bluetooth.host_stopped);
    nimble_port_freertos_deinit();
}

/** @brief Processa resultados e término de uma busca BLE. */
static int driver_bluetooth_gap_cb(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_hs_adv_fields fields = {0};
        ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (s_bluetooth.device_count < DRIVER_BLUETOOTH_MAX_DEVICES) {
            driver_bluetooth_device_t *device = &s_bluetooth.devices[s_bluetooth.device_count++];
            if (fields.name != NULL && fields.name_len > 0) {
                const uint8_t length = fields.name_len > DRIVER_BLUETOOTH_NAME_MAX_LENGTH ?
                                           DRIVER_BLUETOOTH_NAME_MAX_LENGTH : fields.name_len;
                memcpy(device->name, fields.name, length);
                device->name[length] = '\0';
            } else {
                strcpy(device->name, "Dispositivo sem nome");
            }
            snprintf(device->address, sizeof(device->address), "%02X:%02X:%02X:%02X:%02X:%02X",
                     event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
                     event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0]);
            device->rssi = event->disc.rssi;
            if (s_bluetooth.callback != NULL) {
                s_bluetooth.callback(DRIVER_BLUETOOTH_EVENT_DEVICE_FOUND, s_bluetooth.callback_context);
            }
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        s_bluetooth.scanning = false;
        xSemaphoreGive(s_bluetooth.scan_finished);
        if (s_bluetooth.callback != NULL) {
            s_bluetooth.callback(DRIVER_BLUETOOTH_EVENT_SCAN_COMPLETE, s_bluetooth.callback_context);
        }
    }
    return 0;
}

/** @brief Inicializa a pilha NimBLE para operação apenas como scanner BLE. */
esp_err_t driver_bluetooth_init(driver_bluetooth_event_cb_t callback, void *context)
{
    if (s_bluetooth.initialized) {
        s_bluetooth.callback = callback;
        s_bluetooth.callback_context = context;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(nimble_port_init(), "driver_bluetooth", "falha ao iniciar NimBLE");
    s_bluetooth.host_stopped = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_bluetooth.host_stopped != NULL, ESP_ERR_NO_MEM, "driver_bluetooth", "sem memoria para sincronizar NimBLE");
    s_bluetooth.scan_finished = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_bluetooth.scan_finished != NULL, ESP_ERR_NO_MEM, "driver_bluetooth", "sem memoria para sincronizar scan BLE");
    s_bluetooth.callback = callback;
    s_bluetooth.callback_context = context;
    s_bluetooth.initialized = true;
    ble_hs_cfg.sync_cb = driver_bluetooth_on_sync;
    nimble_port_freertos_init(driver_bluetooth_host_task);
    return ESP_OK;
}

/** @brief Para a varredura e libera a pilha NimBLE e o controlador BLE. */
esp_err_t driver_bluetooth_deinit(void)
{
    if (!s_bluetooth.initialized) {
        return ESP_OK;
    }
    if (s_bluetooth.ready) {
        if (s_bluetooth.scanning) {
            ESP_RETURN_ON_FALSE(ble_gap_disc_cancel() == 0, ESP_FAIL, "driver_bluetooth", "falha ao cancelar scan BLE");
            ESP_RETURN_ON_FALSE(xSemaphoreTake(s_bluetooth.scan_finished, pdMS_TO_TICKS(2000)) == pdTRUE,
                                ESP_ERR_TIMEOUT, "driver_bluetooth", "timeout ao encerrar scan BLE");
        }
    }
    const int result = nimble_port_stop();
    ESP_RETURN_ON_FALSE(result == 0, ESP_FAIL, "driver_bluetooth", "falha ao parar NimBLE: %d", result);
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_bluetooth.host_stopped, pdMS_TO_TICKS(2000)) == pdTRUE,
                        ESP_ERR_TIMEOUT, "driver_bluetooth", "timeout ao parar task NimBLE");
    ESP_RETURN_ON_ERROR(nimble_port_deinit(), "driver_bluetooth", "falha ao liberar NimBLE");
    vSemaphoreDelete(s_bluetooth.host_stopped);
    s_bluetooth.host_stopped = NULL;
    vSemaphoreDelete(s_bluetooth.scan_finished);
    s_bluetooth.scan_finished = NULL;
    s_bluetooth.initialized = false;
    s_bluetooth.ready = false;
    s_bluetooth.scanning = false;
    s_bluetooth.device_count = 0;
    return ESP_OK;
}

/** @brief Liga ou desliga a busca por dispositivos Bluetooth Low Energy. */
esp_err_t driver_bluetooth_set_enabled(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_bluetooth.initialized && s_bluetooth.ready, ESP_ERR_INVALID_STATE, "driver_bluetooth", "BLE nao esta pronto");
    if (!enabled) {
        ble_gap_disc_cancel();
        return ESP_OK;
    }
    s_bluetooth.device_count = 0;
    struct ble_gap_disc_params parameters = {0};
    parameters.passive = 0;
    parameters.filter_duplicates = 1;
    if (ble_gap_disc(s_bluetooth.address_type, BLE_HS_FOREVER, &parameters, driver_bluetooth_gap_cb, NULL) != 0) {
        return ESP_FAIL;
    }
    s_bluetooth.scanning = true;
    return ESP_OK;
}

/** @brief Copia os dispositivos descobertos desde a última ativação. */
esp_err_t driver_bluetooth_get_devices(driver_bluetooth_device_t *devices, uint16_t capacity, uint16_t *out_count)
{
    ESP_RETURN_ON_FALSE(devices != NULL && out_count != NULL, ESP_ERR_INVALID_ARG, "driver_bluetooth", "argumento invalido");
    const uint16_t count = s_bluetooth.device_count < capacity ? s_bluetooth.device_count : capacity;
    memcpy(devices, s_bluetooth.devices, count * sizeof(*devices));
    *out_count = count;
    return ESP_OK;
}
