#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Quantidade máxima de relatórios mantidos na NVS. */
#define REPORT_STORAGE_MAX_RECORDS 100U

/** @brief Tamanho máximo dos campos textuais de um relatório. */
#define REPORT_STORAGE_TEXT_LENGTH 96U

/** @brief Dados persistidos para um ciclo concluído. */
typedef struct {
    uint32_t sequence;
    uint32_t total_seconds;
    uint32_t cycles_executed;
    uint32_t rpm;
    uint16_t pressure_bar;
    uint16_t pulse_ms;
    char injector_type[32];
    char test_description[REPORT_STORAGE_TEXT_LENGTH];
    char date[11];
    char start_time[6];
    char end_time[6];
    char customer_name[REPORT_STORAGE_TEXT_LENGTH];
    char phone[REPORT_STORAGE_TEXT_LENGTH];
    char vehicle[REPORT_STORAGE_TEXT_LENGTH];
    char plate[REPORT_STORAGE_TEXT_LENGTH];
    char mileage[REPORT_STORAGE_TEXT_LENGTH];
    char observations[REPORT_STORAGE_TEXT_LENGTH];
} report_storage_record_t;

/**
 * @brief Inicializa os metadados persistentes da área de relatórios.
 *
 * @return ESP_OK em caso de sucesso ou erro da NVS.
 */
esp_err_t report_storage_init(void);

/**
 * @brief Salva um relatório na posição circular seguinte.
 *
 * Ao exceder REPORT_STORAGE_MAX_RECORDS, substitui automaticamente o relatório mais antigo.
 *
 * @param[in] record Dados do relatório a serem persistidos.
 * @param[out] out_sequence Sequência atribuída ao relatório, opcional.
 * @return ESP_OK em caso de sucesso ou erro da NVS.
 */
esp_err_t report_storage_save(const report_storage_record_t *record, uint32_t *out_sequence);

/**
 * @brief Consulta quantos relatórios estão disponíveis no armazenamento circular.
 *
 * @param[out] out_count Ponteiro que recebe a quantidade de relatórios armazenados.
 * @return ESP_OK em caso de sucesso ou erro da NVS.
 */
esp_err_t report_storage_get_count(uint32_t *out_count);

/**
 * @brief Lê um relatório pela posição relativa à gravação mais recente.
 *
 * O índice zero corresponde ao último relatório salvo, o índice um ao anterior e assim por diante.
 *
 * @param[in] recent_index Índice relativo, entre zero e a quantidade de relatórios menos um.
 * @param[out] out_record Ponteiro que recebe o relatório armazenado.
 * @return ESP_OK em caso de sucesso, ESP_ERR_NOT_FOUND se o índice não existir ou erro da NVS.
 */
esp_err_t report_storage_get_recent(uint32_t recent_index, report_storage_record_t *out_record);

/**
 * @brief Lê um relatório pela posição relativa à gravação mais recente.
 *
 * O índice zero corresponde ao último relatório salvo, o índice um ao anterior e assim por diante.
 *
 * @param[in] recent_index Índice relativo, entre zero e a quantidade de relatórios menos um.
 * @param[out] out_record Ponteiro que recebe o relatório armazenado.
 * @return ESP_OK em caso de sucesso, ESP_ERR_NOT_FOUND se o índice não existir ou erro da NVS.
 */
esp_err_t report_storage_get_recent(uint32_t recent_index, report_storage_record_t *out_record);

#ifdef __cplusplus
}
#endif
