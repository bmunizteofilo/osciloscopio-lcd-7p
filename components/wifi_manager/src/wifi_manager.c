#include "wifi_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define WIFI_MANAGER_TASK_STACK_SIZE 4096
#define WIFI_MANAGER_TASK_PRIORITY 5
#define WIFI_MANAGER_QUEUE_LENGTH 12

/** @brief Comando ou notificação processada pela task do core 0. */
typedef enum {
    WIFI_MANAGER_COMMAND_SET_ENABLED,
    WIFI_MANAGER_COMMAND_CONNECT,
    WIFI_MANAGER_COMMAND_EVENT,
} wifi_manager_command_type_t;

/** @brief Mensagem interna do gerenciador. */
typedef struct {
    wifi_manager_command_type_t type;
    union {
        bool enabled;
        driver_wifi_event_t event;
        struct {
            char ssid[DRIVER_WIFI_SSID_MAX_LENGTH + 1];
            char password[65];
        } credentials;
    } data;
} wifi_manager_command_t;

/** @brief Contexto protegido e compartilhado com a UI. */
typedef struct {
    QueueHandle_t queue;
    SemaphoreHandle_t lock;
    wifi_manager_status_t status;
} wifi_manager_context_t;

/** @brief Estado persistente do Wi-Fi da aplicação. */
static wifi_manager_context_t s_wifi_manager;

/**
 * @brief Recebe eventos do driver e os encaminha à task do core 0.
 *
 * @param[in] event Evento do driver Wi-Fi.
 * @param[in] context Contexto do gerenciador.
 */
static void wifi_manager_driver_event_cb(driver_wifi_event_t event, void *context)
{
    wifi_manager_context_t *manager = context;
    const wifi_manager_command_t command = {.type = WIFI_MANAGER_COMMAND_EVENT, .data.event = event};
    xQueueSend(manager->queue, &command, 0);
}

/**
 * @brief Atualiza a lista pública de redes após o término de uma busca.
 */
static void wifi_manager_update_scan_results(void)
{
    wifi_manager_status_t status = {0};
    if (xSemaphoreTake(s_wifi_manager.lock, portMAX_DELAY) == pdTRUE) {
        status = s_wifi_manager.status;
        xSemaphoreGive(s_wifi_manager.lock);
    }
    status.scanning = false;
    driver_wifi_get_scan_results(status.networks, DRIVER_WIFI_MAX_NETWORKS, &status.network_count);
    if (xSemaphoreTake(s_wifi_manager.lock, portMAX_DELAY) == pdTRUE) {
        s_wifi_manager.status = status;
        xSemaphoreGive(s_wifi_manager.lock);
    }
}

/**
 * @brief Processa comandos de Wi-Fi no core 0.
 *
 * @param[in] argument Não utilizado.
 */
static void wifi_manager_task(void *argument)
{
    (void)argument;
    driver_wifi_init(wifi_manager_driver_event_cb, &s_wifi_manager);
    for (;;) {
        wifi_manager_command_t command = {0};
        if (xQueueReceive(s_wifi_manager.queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command.type == WIFI_MANAGER_COMMAND_SET_ENABLED) {
            if (driver_wifi_set_enabled(command.data.enabled) != ESP_OK) {
                continue;
            }
            if (xSemaphoreTake(s_wifi_manager.lock, portMAX_DELAY) == pdTRUE) {
                s_wifi_manager.status.enabled = command.data.enabled;
                s_wifi_manager.status.scanning = command.data.enabled;
                s_wifi_manager.status.connected = false;
                s_wifi_manager.status.connecting = false;
                s_wifi_manager.status.connection_failed = false;
                s_wifi_manager.status.connected_ssid[0] = '\0';
                s_wifi_manager.status.network_count = 0;
                xSemaphoreGive(s_wifi_manager.lock);
            }
            if (command.data.enabled && driver_wifi_start_scan() != ESP_OK) {
                if (xSemaphoreTake(s_wifi_manager.lock, portMAX_DELAY) == pdTRUE) {
                    s_wifi_manager.status.scanning = false;
                    xSemaphoreGive(s_wifi_manager.lock);
                }
            }
        } else if (command.type == WIFI_MANAGER_COMMAND_CONNECT) {
            if (driver_wifi_connect(command.data.credentials.ssid, command.data.credentials.password) == ESP_OK &&
                xSemaphoreTake(s_wifi_manager.lock, portMAX_DELAY) == pdTRUE) {
                s_wifi_manager.status.connecting = true;
                s_wifi_manager.status.connection_failed = false;
                strncpy(s_wifi_manager.status.connected_ssid,
                        command.data.credentials.ssid,
                        DRIVER_WIFI_SSID_MAX_LENGTH);
                s_wifi_manager.status.connected_ssid[DRIVER_WIFI_SSID_MAX_LENGTH] = '\0';
                xSemaphoreGive(s_wifi_manager.lock);
            }
        } else if (command.data.event == DRIVER_WIFI_EVENT_SCAN_DONE) {
            wifi_manager_update_scan_results();
        } else if (xSemaphoreTake(s_wifi_manager.lock, portMAX_DELAY) == pdTRUE) {
            if (command.data.event == DRIVER_WIFI_EVENT_CONNECTED) {
                s_wifi_manager.status.connected = true;
                s_wifi_manager.status.connecting = false;
            } else {
                s_wifi_manager.status.connection_failed = s_wifi_manager.status.connecting;
                s_wifi_manager.status.connected = false;
                s_wifi_manager.status.connecting = false;
                if (s_wifi_manager.status.connection_failed) {
                    s_wifi_manager.status.connected_ssid[0] = '\0';
                }
            }
            xSemaphoreGive(s_wifi_manager.lock);
        }
    }
}

/**
 * @brief Cria a task gerenciadora do Wi-Fi fixada no core 0.
 *
 * @return @c ESP_OK em caso de sucesso ou erro de memória.
 */
esp_err_t wifi_manager_start(void)
{
    if (s_wifi_manager.queue != NULL) {
        return ESP_OK;
    }
    s_wifi_manager.queue = xQueueCreate(WIFI_MANAGER_QUEUE_LENGTH, sizeof(wifi_manager_command_t));
    s_wifi_manager.lock = xSemaphoreCreateMutex();
    if (s_wifi_manager.queue == NULL || s_wifi_manager.lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreatePinnedToCore(wifi_manager_task,
                                   "wifi_mgr",
                                   WIFI_MANAGER_TASK_STACK_SIZE,
                                   NULL,
                                   WIFI_MANAGER_TASK_PRIORITY,
                                   NULL,
                                   0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

/**
 * @brief Solicita ligar ou desligar o Wi-Fi de forma assíncrona.
 *
 * @param[in] enabled Novo estado solicitado.
 * @return @c ESP_OK ao enfileirar o comando ou erro se o gerenciador não iniciou.
 */
esp_err_t wifi_manager_set_enabled(bool enabled)
{
    if (s_wifi_manager.queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const wifi_manager_command_t command = {.type = WIFI_MANAGER_COMMAND_SET_ENABLED, .data.enabled = enabled};
    return xQueueSend(s_wifi_manager.queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

/**
 * @brief Solicita a conexão com uma rede usando as credenciais informadas.
 *
 * @param[in] ssid Nome da rede Wi-Fi.
 * @param[in] password Senha da rede; pode ser vazia.
 * @return @c ESP_OK ao enfileirar o comando ou erro de argumento/estado.
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (s_wifi_manager.queue == NULL || ssid == NULL || password == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_manager_command_t command = {.type = WIFI_MANAGER_COMMAND_CONNECT};
    strncpy(command.data.credentials.ssid, ssid, DRIVER_WIFI_SSID_MAX_LENGTH);
    command.data.credentials.ssid[DRIVER_WIFI_SSID_MAX_LENGTH] = '\0';
    strncpy(command.data.credentials.password, password, sizeof(command.data.credentials.password) - 1);
    command.data.credentials.password[sizeof(command.data.credentials.password) - 1] = '\0';
    return xQueueSend(s_wifi_manager.queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

/**
 * @brief Copia o estado e a última lista de redes disponíveis.
 *
 * @param[out] out_status Estrutura que recebe o estado atual.
 * @return @c ESP_OK em caso de sucesso ou erro de argumento.
 */
esp_err_t wifi_manager_get_status(wifi_manager_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_wifi_manager.lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out_status = s_wifi_manager.status;
    xSemaphoreGive(s_wifi_manager.lock);
    return ESP_OK;
}
