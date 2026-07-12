#include "bluetooth_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BLUETOOTH_MANAGER_TASK_STACK_SIZE 4096
#define BLUETOOTH_MANAGER_TASK_PRIORITY 5
#define BLUETOOTH_MANAGER_QUEUE_LENGTH 8

/** @brief Tipo de mensagem processada pela task Bluetooth. */
typedef enum {
    BLUETOOTH_MANAGER_COMMAND_SET_ENABLED,
    BLUETOOTH_MANAGER_COMMAND_EVENT,
} bluetooth_manager_command_type_t;

/** @brief Mensagem interna do gerenciador Bluetooth. */
typedef struct {
    bluetooth_manager_command_type_t type;
    union {
        bool enabled;
        driver_bluetooth_event_t event;
    } data;
} bluetooth_manager_command_t;

/** @brief Contexto compartilhado entre task Bluetooth e interface. */
typedef struct {
    QueueHandle_t queue;
    SemaphoreHandle_t lock;
    bluetooth_manager_status_t status;
    bool initialized;
} bluetooth_manager_context_t;

/** @brief Estado persistente do gerenciador Bluetooth. */
static bluetooth_manager_context_t s_bluetooth_manager;

/** @brief Encaminha eventos do driver para a task fixada no core 0. */
static void bluetooth_manager_driver_event_cb(driver_bluetooth_event_t event, void *context)
{
    bluetooth_manager_context_t *manager = context;
    const bluetooth_manager_command_t command = {.type = BLUETOOTH_MANAGER_COMMAND_EVENT, .data.event = event};
    xQueueSend(manager->queue, &command, 0);
}

/** @brief Atualiza a cópia pública de dispositivos BLE. */
static void bluetooth_manager_update_devices(void)
{
    bluetooth_manager_status_t status = {0};
    if (xSemaphoreTake(s_bluetooth_manager.lock, portMAX_DELAY) == pdTRUE) {
        status = s_bluetooth_manager.status;
        xSemaphoreGive(s_bluetooth_manager.lock);
    }
    if (!status.enabled) {
        return;
    }
    driver_bluetooth_get_devices(status.devices, DRIVER_BLUETOOTH_MAX_DEVICES, &status.device_count);
    if (xSemaphoreTake(s_bluetooth_manager.lock, portMAX_DELAY) == pdTRUE) {
        s_bluetooth_manager.status = status;
        xSemaphoreGive(s_bluetooth_manager.lock);
    }
}

/** @brief Processa comandos Bluetooth no core 0. */
static void bluetooth_manager_task(void *argument)
{
    (void)argument;
    for (;;) {
        bluetooth_manager_command_t command = {0};
        if (xQueueReceive(s_bluetooth_manager.queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command.type == BLUETOOTH_MANAGER_COMMAND_SET_ENABLED) {
            if (command.data.enabled && !s_bluetooth_manager.initialized) {
                if (driver_bluetooth_init(bluetooth_manager_driver_event_cb, &s_bluetooth_manager) != ESP_OK) {
                    continue;
                }
                s_bluetooth_manager.initialized = true;
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            if (!command.data.enabled && driver_bluetooth_deinit() != ESP_OK) {
                continue;
            }
            if (command.data.enabled && driver_bluetooth_set_enabled(true) != ESP_OK) {
                continue;
            }
            if (!command.data.enabled) {
                s_bluetooth_manager.initialized = false;
            }
            if (xSemaphoreTake(s_bluetooth_manager.lock, portMAX_DELAY) == pdTRUE) {
                s_bluetooth_manager.status.enabled = command.data.enabled;
                s_bluetooth_manager.status.scanning = command.data.enabled;
                s_bluetooth_manager.status.device_count = 0;
                xSemaphoreGive(s_bluetooth_manager.lock);
            }
        } else {
            bluetooth_manager_update_devices();
            if (command.data.event == DRIVER_BLUETOOTH_EVENT_SCAN_COMPLETE &&
                xSemaphoreTake(s_bluetooth_manager.lock, portMAX_DELAY) == pdTRUE) {
                s_bluetooth_manager.status.scanning = false;
                xSemaphoreGive(s_bluetooth_manager.lock);
            }
        }
    }
}

/** @brief Cria a task gerenciadora de Bluetooth fixada no core 0. */
esp_err_t bluetooth_manager_start(void)
{
    if (s_bluetooth_manager.queue != NULL) {
        return ESP_OK;
    }
    s_bluetooth_manager.queue = xQueueCreate(BLUETOOTH_MANAGER_QUEUE_LENGTH, sizeof(bluetooth_manager_command_t));
    s_bluetooth_manager.lock = xSemaphoreCreateMutex();
    if (s_bluetooth_manager.queue == NULL || s_bluetooth_manager.lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreatePinnedToCore(bluetooth_manager_task, "bt_mgr", BLUETOOTH_MANAGER_TASK_STACK_SIZE, NULL,
                                   BLUETOOTH_MANAGER_TASK_PRIORITY, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

/** @brief Solicita ligar ou desligar a varredura Bluetooth de forma assíncrona. */
esp_err_t bluetooth_manager_set_enabled(bool enabled)
{
    if (s_bluetooth_manager.queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!enabled && xSemaphoreTake(s_bluetooth_manager.lock, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_bluetooth_manager.status = (bluetooth_manager_status_t){0};
        xSemaphoreGive(s_bluetooth_manager.lock);
    }
    const bluetooth_manager_command_t command = {.type = BLUETOOTH_MANAGER_COMMAND_SET_ENABLED, .data.enabled = enabled};
    return xQueueSend(s_bluetooth_manager.queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

/** @brief Copia o estado e os dispositivos Bluetooth encontrados. */
esp_err_t bluetooth_manager_get_status(bluetooth_manager_status_t *out_status)
{
    if (out_status == NULL || s_bluetooth_manager.lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_bluetooth_manager.lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out_status = s_bluetooth_manager.status;
    xSemaphoreGive(s_bluetooth_manager.lock);
    return ESP_OK;
}
