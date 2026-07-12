#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DRIVER_WIFI_MAX_NETWORKS 12
#define DRIVER_WIFI_SSID_MAX_LENGTH 32

/** @brief Evento recebido do Wi-Fi do ESP-IDF. */
typedef enum {
    DRIVER_WIFI_EVENT_SCAN_DONE,
    DRIVER_WIFI_EVENT_CONNECTED,
    DRIVER_WIFI_EVENT_DISCONNECTED,
} driver_wifi_event_t;

/** @brief Rede Wi-Fi encontrada durante uma varredura. */
typedef struct {
    char ssid[DRIVER_WIFI_SSID_MAX_LENGTH + 1];
    int8_t rssi;
    bool secured;
} driver_wifi_network_t;

/** @brief Função chamada pelo driver ao receber eventos do ESP-IDF. */
typedef void (*driver_wifi_event_cb_t)(driver_wifi_event_t event, void *context);

/**
 * @brief Inicializa NVS, netif, event loop e o modo estação Wi-Fi.
 *
 * @param[in] callback Função que recebe eventos assíncronos do Wi-Fi.
 * @param[in] context Contexto entregue à função de callback.
 * @return @c ESP_OK em caso de sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_init(driver_wifi_event_cb_t callback, void *context);

/**
 * @brief Liga ou desliga a estação Wi-Fi.
 *
 * @param[in] enabled @c true para iniciar ou @c false para parar o Wi-Fi.
 * @return @c ESP_OK em caso de sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_set_enabled(bool enabled);

/**
 * @brief Inicia uma busca assíncrona por redes Wi-Fi.
 *
 * @return @c ESP_OK se a busca foi iniciada ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_start_scan(void);

/**
 * @brief Configura credenciais e inicia a conexão com uma rede.
 *
 * @param[in] ssid Nome da rede Wi-Fi.
 * @param[in] password Senha da rede; pode ser vazia para redes abertas.
 * @return @c ESP_OK em caso de sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_connect(const char *ssid, const char *password);

/**
 * @brief Copia as redes encontradas na última busca concluída.
 *
 * @param[out] networks Vetor de redes de destino.
 * @param[in] capacity Quantidade máxima de itens em @p networks.
 * @param[out] out_count Quantidade de redes copiadas.
 * @return @c ESP_OK em caso de sucesso ou erro do ESP-IDF.
 */
esp_err_t driver_wifi_get_scan_results(driver_wifi_network_t *networks, uint16_t capacity, uint16_t *out_count);

#ifdef __cplusplus
}
#endif
