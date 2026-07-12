#include "driver_wifi.h"

#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

/** @brief Contexto interno do wrapper de Wi-Fi. */
typedef struct {
    driver_wifi_event_cb_t callback;
    void *callback_context;
    bool initialized;
    bool enabled;
} driver_wifi_context_t;

/** @brief Estado persistente do wrapper. */
static driver_wifi_context_t s_driver_wifi;

/**
 * @brief Encaminha eventos da estação e da varredura ao consumidor do driver.
 *
 * @param[in] argument Não utilizado.
 * @param[in] event_base Base do evento ESP-IDF.
 * @param[in] event_id Identificador do evento ESP-IDF.
 * @param[in] event_data Dados opcionais do evento.
 */
static void driver_wifi_event_handler(void *argument, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    if (s_driver_wifi.callback == NULL) {
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        s_driver_wifi.callback(DRIVER_WIFI_EVENT_SCAN_DONE, s_driver_wifi.callback_context);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        s_driver_wifi.callback(DRIVER_WIFI_EVENT_CONNECTED, s_driver_wifi.callback_context);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_driver_wifi.callback(DRIVER_WIFI_EVENT_DISCONNECTED, s_driver_wifi.callback_context);
    }
}

/**
 * @brief Inicializa NVS, netif, event loop e o modo estação Wi-Fi.
 *
 * @param[in] callback Função que recebe eventos assíncronos do Wi-Fi.
 * @param[in] context Contexto entregue à função de callback.
 * @return @c ESP_OK em caso de sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_init(driver_wifi_event_cb_t callback, void *context)
{
    if (s_driver_wifi.initialized) {
        return ESP_OK;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), "driver_wifi", "falha ao apagar NVS");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, "driver_wifi", "falha ao inicializar NVS");
    err = esp_netif_init();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, "driver_wifi", "falha ao inicializar netif");
    err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, "driver_wifi", "falha ao criar event loop");
    esp_netif_create_default_wifi_sta();
    const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), "driver_wifi", "falha ao inicializar Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), "driver_wifi", "falha ao selecionar armazenamento RAM");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), "driver_wifi", "falha ao configurar estação Wi-Fi");
    s_driver_wifi.callback = callback;
    s_driver_wifi.callback_context = context;
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, driver_wifi_event_handler, NULL, NULL),
                        "driver_wifi", "falha ao registrar eventos Wi-Fi");
    s_driver_wifi.initialized = true;
    return ESP_OK;
}

/**
 * @brief Liga ou desliga a estação Wi-Fi.
 *
 * @param[in] enabled @c true para iniciar ou @c false para parar o Wi-Fi.
 * @return @c ESP_OK em caso de sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_set_enabled(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_driver_wifi.initialized, ESP_ERR_INVALID_STATE, "driver_wifi", "Wi-Fi nao inicializado");
    if (enabled == s_driver_wifi.enabled) {
        return ESP_OK;
    }
    esp_err_t err = enabled ? esp_wifi_start() : esp_wifi_stop();
    ESP_RETURN_ON_ERROR(err, "driver_wifi", "falha ao alterar estado Wi-Fi");
    s_driver_wifi.enabled = enabled;
    return ESP_OK;
}

/**
 * @brief Inicia uma busca assíncrona por redes Wi-Fi.
 *
 * @return @c ESP_OK se a busca foi iniciada ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_start_scan(void)
{
    ESP_RETURN_ON_FALSE(s_driver_wifi.enabled, ESP_ERR_INVALID_STATE, "driver_wifi", "Wi-Fi desligado");
    return esp_wifi_scan_start(NULL, false);
}

/**
 * @brief Configura credenciais e inicia a conexão com uma rede.
 *
 * @param[in] ssid Nome da rede Wi-Fi.
 * @param[in] password Senha da rede; pode ser vazia para redes abertas.
 * @return @c ESP_OK em caso de sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_connect(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(s_driver_wifi.enabled && ssid != NULL && password != NULL,
                        ESP_ERR_INVALID_STATE,
                        "driver_wifi",
                        "Wi-Fi desligado ou credencial invalida");
    wifi_config_t config = {0};
    strncpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid) - 1);
    strncpy((char *)config.sta.password, password, sizeof(config.sta.password) - 1);
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), "driver_wifi", "falha ao configurar rede");
    return esp_wifi_connect();
}

/**
 * @brief Copia as redes encontradas na última busca concluída.
 *
 * @param[out] networks Vetor de redes de destino.
 * @param[in] capacity Quantidade máxima de itens em @p networks.
 * @param[out] out_count Quantidade de redes copiadas.
 * @return @c ESP_OK em caso de sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_get_scan_results(driver_wifi_network_t *networks, uint16_t capacity, uint16_t *out_count)
{
    ESP_RETURN_ON_FALSE(networks != NULL && out_count != NULL, ESP_ERR_INVALID_ARG, "driver_wifi", "argumento invalido");
    uint16_t found = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&found), "driver_wifi", "falha ao obter quantidade de redes");
    const uint16_t count = found < capacity ? found : capacity;
    wifi_ap_record_t records[DRIVER_WIFI_MAX_NETWORKS] = {0};
    uint16_t record_count = count;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&record_count, records), "driver_wifi", "falha ao obter redes");
    for (uint16_t index = 0; index < record_count; index++) {
        strncpy(networks[index].ssid, (const char *)records[index].ssid, DRIVER_WIFI_SSID_MAX_LENGTH);
        networks[index].ssid[DRIVER_WIFI_SSID_MAX_LENGTH] = '\0';
        networks[index].rssi = records[index].rssi;
        networks[index].secured = records[index].authmode != WIFI_AUTH_OPEN;
    }
    *out_count = record_count;
    return ESP_OK;
}
