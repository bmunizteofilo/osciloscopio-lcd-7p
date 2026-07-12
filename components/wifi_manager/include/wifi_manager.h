#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver_wifi.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Estado público consumido pela interface LVGL. */
typedef struct {
    bool enabled;
    bool scanning;
    bool connected;
    bool connecting;
    bool connection_failed;
    char connected_ssid[DRIVER_WIFI_SSID_MAX_LENGTH + 1];
    uint16_t network_count;
    driver_wifi_network_t networks[DRIVER_WIFI_MAX_NETWORKS];
} wifi_manager_status_t;

/**
 * @brief Cria a task gerenciadora do Wi-Fi fixada no core 0.
 *
 * @return @c ESP_OK em caso de sucesso ou erro de memória.
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Solicita ligar ou desligar o Wi-Fi de forma assíncrona.
 *
 * @param[in] enabled Novo estado solicitado.
 * @return @c ESP_OK ao enfileirar o comando ou erro se o gerenciador não iniciou.
 */
esp_err_t wifi_manager_set_enabled(bool enabled);

/**
 * @brief Solicita a conexão com uma rede usando as credenciais informadas.
 *
 * @param[in] ssid Nome da rede Wi-Fi.
 * @param[in] password Senha da rede; pode ser vazia.
 * @return @c ESP_OK ao enfileirar o comando ou erro de argumento/estado.
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Copia o estado e a última lista de redes disponíveis.
 *
 * @param[out] out_status Estrutura que recebe o estado atual.
 * @return @c ESP_OK em caso de sucesso ou erro de argumento.
 */
esp_err_t wifi_manager_get_status(wifi_manager_status_t *out_status);

#ifdef __cplusplus
}
#endif
