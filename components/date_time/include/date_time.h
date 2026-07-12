#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia a task de baixa prioridade que mantém e sincroniza data e hora.
 *
 * @return @c ESP_OK em caso de sucesso.
 */
esp_err_t date_time_start(void);

/**
 * @brief Informa se o relógio já recebeu uma data válida por SNTP.
 *
 * @return @c true quando a data e hora estão sincronizadas.
 */
bool date_time_is_synchronized(void);

/**
 * @brief Formata a hora local atual no formato HH:MM.
 *
 * @param[out] buffer Buffer de destino.
 * @param[in] size Tamanho do buffer de destino.
 */
void date_time_format_time(char *buffer, size_t size);

/**
 * @brief Formata a data local atual no formato DD/MM/AAAA.
 *
 * @param[out] buffer Buffer de destino.
 * @param[in] size Tamanho do buffer de destino.
 */
void date_time_format_date(char *buffer, size_t size);

/**
 * @brief Ajusta manualmente data e hora locais.
 *
 * @param[in] year Ano local.
 * @param[in] month Mês local entre 1 e 12.
 * @param[in] day Dia local entre 1 e 31.
 * @param[in] hour Hora local entre 0 e 23.
 * @param[in] minute Minuto local entre 0 e 59.
 *
 * @return @c ESP_OK quando o relógio do sistema foi atualizado.
 */
esp_err_t date_time_set_local(uint32_t year, uint32_t month, uint32_t day,
                              uint32_t hour, uint32_t minute);

#ifdef __cplusplus
}
#endif
