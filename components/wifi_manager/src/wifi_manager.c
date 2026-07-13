#include "wifi_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define WIFI_MANAGER_TASK_STACK_SIZE 4096
#define WIFI_MANAGER_TASK_PRIORITY 5
#define WIFI_MANAGER_QUEUE_LENGTH 12
#define WIFI_MANAGER_MAX_SAVED_NETWORKS 10
#define WIFI_MANAGER_STORAGE_NAMESPACE "redes_wifi"
#define WIFI_MANAGER_STORAGE_KEY_ENABLED "ativado"
#define WIFI_MANAGER_STORAGE_KEY_PROFILES "perfis"
#define WIFI_MANAGER_STORAGE_VERSION 1U

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

/** @brief Perfil de rede salvo de forma persistente na NVS. */
typedef struct {
    char ssid[DRIVER_WIFI_SSID_MAX_LENGTH + 1];
    char password[65];
    uint32_t last_used_order;
} wifi_manager_saved_network_t;

/** @brief Estrutura serializada no namespace NVS de redes Wi-Fi. */
typedef struct {
    uint32_t version;
    uint32_t next_order;
    uint8_t count;
    wifi_manager_saved_network_t networks[WIFI_MANAGER_MAX_SAVED_NETWORKS];
} wifi_manager_saved_networks_t;

/** @brief Contexto protegido e compartilhado com a UI. */
typedef struct {
    QueueHandle_t queue;
    SemaphoreHandle_t lock;
    wifi_manager_status_t status;
    wifi_manager_saved_networks_t saved_networks;
    bool pending_manual_profile;
    char pending_ssid[DRIVER_WIFI_SSID_MAX_LENGTH + 1];
    char pending_password[65];
} wifi_manager_context_t;

/** @brief Estado persistente do Wi-Fi da aplicação. */
static wifi_manager_context_t s_wifi_manager;

/** @brief Restaura o estado ligado/desligado salvo para o Wi-Fi. */
static bool wifi_manager_load_enabled_state(void)
{
    nvs_handle_t handle;
    uint8_t enabled = 0;
    if (nvs_open(WIFI_MANAGER_STORAGE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    const esp_err_t err = nvs_get_u8(handle, WIFI_MANAGER_STORAGE_KEY_ENABLED, &enabled);
    nvs_close(handle);
    return err == ESP_OK && enabled != 0;
}

/** @brief Persiste o estado ligado/desligado escolhido pelo usuário. */
static void wifi_manager_save_enabled_state(bool enabled)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_MANAGER_STORAGE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    if (nvs_set_u8(handle, WIFI_MANAGER_STORAGE_KEY_ENABLED, enabled ? 1U : 0U) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

/** @brief Carrega os perfis Wi-Fi salvos, descartando dados inválidos. */
static void wifi_manager_load_saved_networks(void)
{
    wifi_manager_saved_networks_t saved_networks = {.version = WIFI_MANAGER_STORAGE_VERSION, .next_order = 1U};
    nvs_handle_t handle;
    size_t size = sizeof(saved_networks);
    if (nvs_open(WIFI_MANAGER_STORAGE_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_blob(handle, WIFI_MANAGER_STORAGE_KEY_PROFILES, &saved_networks, &size) != ESP_OK ||
            size != sizeof(saved_networks) || saved_networks.version != WIFI_MANAGER_STORAGE_VERSION ||
            saved_networks.count > WIFI_MANAGER_MAX_SAVED_NETWORKS) {
            saved_networks = (wifi_manager_saved_networks_t){.version = WIFI_MANAGER_STORAGE_VERSION, .next_order = 1U};
        }
        nvs_close(handle);
    }
    if (saved_networks.next_order == 0U) {
        saved_networks.next_order = 1U;
    }
    s_wifi_manager.saved_networks = saved_networks;
}

/** @brief Salva a lista atual de até dez perfis Wi-Fi na NVS. */
static void wifi_manager_save_saved_networks(void)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_MANAGER_STORAGE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(handle, WIFI_MANAGER_STORAGE_KEY_PROFILES, &s_wifi_manager.saved_networks,
                     sizeof(s_wifi_manager.saved_networks)) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

/** @brief Localiza um perfil salvo pelo SSID ou retorna -1 quando inexistente. */
static int32_t wifi_manager_find_saved_network(const char *ssid)
{
    for (uint8_t index = 0; index < s_wifi_manager.saved_networks.count; index++) {
        if (strcmp(s_wifi_manager.saved_networks.networks[index].ssid, ssid) == 0) {
            return index;
        }
    }
    return -1;
}

/** @brief Salva ou atualiza o perfil cuja conexão manual acabou de ser confirmada. */
static void wifi_manager_store_pending_manual_profile(void)
{
    if (!s_wifi_manager.pending_manual_profile) {
        return;
    }
    int32_t index = wifi_manager_find_saved_network(s_wifi_manager.pending_ssid);
    if (index < 0 && s_wifi_manager.saved_networks.count < WIFI_MANAGER_MAX_SAVED_NETWORKS) {
        index = s_wifi_manager.saved_networks.count++;
    }
    if (index < 0) {
        index = 0;
        for (uint8_t candidate = 1; candidate < WIFI_MANAGER_MAX_SAVED_NETWORKS; candidate++) {
            if (s_wifi_manager.saved_networks.networks[candidate].last_used_order <
                s_wifi_manager.saved_networks.networks[index].last_used_order) {
                index = candidate;
            }
        }
    }
    wifi_manager_saved_network_t *profile = &s_wifi_manager.saved_networks.networks[index];
    strncpy(profile->ssid, s_wifi_manager.pending_ssid, DRIVER_WIFI_SSID_MAX_LENGTH);
    profile->ssid[DRIVER_WIFI_SSID_MAX_LENGTH] = '\0';
    strncpy(profile->password, s_wifi_manager.pending_password, sizeof(profile->password) - 1);
    profile->password[sizeof(profile->password) - 1] = '\0';
    profile->last_used_order = s_wifi_manager.saved_networks.next_order++;
    if (s_wifi_manager.saved_networks.next_order == 0U) {
        s_wifi_manager.saved_networks.next_order = 1U;
    }
    wifi_manager_save_saved_networks();
    s_wifi_manager.pending_manual_profile = false;
}

/** @brief Tenta conectar à rede salva mais recentemente usada e visível no scan. */
static void wifi_manager_try_saved_network(void)
{
    int32_t selected_index = -1;
    for (uint16_t network_index = 0; network_index < s_wifi_manager.status.network_count; network_index++) {
        const int32_t saved_index = wifi_manager_find_saved_network(s_wifi_manager.status.networks[network_index].ssid);
        if (saved_index >= 0 && (selected_index < 0 ||
                                 s_wifi_manager.saved_networks.networks[saved_index].last_used_order >
                                 s_wifi_manager.saved_networks.networks[selected_index].last_used_order)) {
            selected_index = saved_index;
        }
    }
    if (selected_index < 0 || driver_wifi_connect(s_wifi_manager.saved_networks.networks[selected_index].ssid,
                                                   s_wifi_manager.saved_networks.networks[selected_index].password) != ESP_OK) {
        return;
    }
    if (xSemaphoreTake(s_wifi_manager.lock, portMAX_DELAY) == pdTRUE) {
        s_wifi_manager.status.connecting = true;
        s_wifi_manager.status.connection_failed = false;
        strncpy(s_wifi_manager.status.connected_ssid, s_wifi_manager.saved_networks.networks[selected_index].ssid,
                DRIVER_WIFI_SSID_MAX_LENGTH);
        s_wifi_manager.status.connected_ssid[DRIVER_WIFI_SSID_MAX_LENGTH] = '\0';
        xSemaphoreGive(s_wifi_manager.lock);
    }
}

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
    if (status.enabled && !status.connected && !status.connecting) {
        wifi_manager_try_saved_network();
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
    if (driver_wifi_init(wifi_manager_driver_event_cb, &s_wifi_manager) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    wifi_manager_load_saved_networks();
    if (wifi_manager_load_enabled_state()) {
        const wifi_manager_command_t command = {.type = WIFI_MANAGER_COMMAND_SET_ENABLED, .data.enabled = true};
        xQueueSend(s_wifi_manager.queue, &command, 0);
    }
    for (;;) {
        wifi_manager_command_t command = {0};
        if (xQueueReceive(s_wifi_manager.queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command.type == WIFI_MANAGER_COMMAND_SET_ENABLED) {
            if (driver_wifi_set_enabled(command.data.enabled) != ESP_OK) {
                continue;
            }
            wifi_manager_save_enabled_state(command.data.enabled);
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
                s_wifi_manager.pending_manual_profile = true;
                strncpy(s_wifi_manager.pending_ssid, command.data.credentials.ssid, DRIVER_WIFI_SSID_MAX_LENGTH);
                s_wifi_manager.pending_ssid[DRIVER_WIFI_SSID_MAX_LENGTH] = '\0';
                strncpy(s_wifi_manager.pending_password, command.data.credentials.password,
                        sizeof(s_wifi_manager.pending_password) - 1);
                s_wifi_manager.pending_password[sizeof(s_wifi_manager.pending_password) - 1] = '\0';
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
                xSemaphoreGive(s_wifi_manager.lock);
                wifi_manager_store_pending_manual_profile();
                continue;
            } else {
                s_wifi_manager.status.connection_failed = s_wifi_manager.status.connecting;
                s_wifi_manager.status.connected = false;
                s_wifi_manager.status.connecting = false;
                if (s_wifi_manager.status.connection_failed) {
                    s_wifi_manager.status.connected_ssid[0] = '\0';
                }
                s_wifi_manager.pending_manual_profile = false;
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
