#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Capacidade por canal do histórico circular em PSRAM. */
#define ACQUISITION_HISTORY_SAMPLES_PER_CHANNEL (500U * 1024U / sizeof(uint16_t))

/** @brief Fotografia consistente do histórico usada exclusivamente pelo leitor. */
typedef struct {
    uint32_t sample_count;
    uint32_t sample_head;
    uint32_t total_frames;
    uint32_t generation;
    uint32_t frame_rate_hz;
} acquisition_history_snapshot_t;

/** @brief Reserva os quatro buffers de histórico na PSRAM. */
esp_err_t acquisition_history_init(void);

/** @brief Descarta todo o histórico e inicia uma nova geração de amostras. */
void acquisition_history_reset(void);

/**
 * @brief Define se novos blocos ADC devem ser descartados sem alterar o histórico.
 *
 * A aquisição SPI continua consumindo os blocos normalmente enquanto a escrita
 * estiver pausada, preservando a janela usada pela interface para análise.
 *
 * @param[in] paused @c true para descartar blocos ou @c false para gravá-los.
 */
void acquisition_history_set_write_paused(bool paused);

/** @brief Insere um bloco ADC intercalado CH1..CH4; chamada somente pelo produtor SPI. */
esp_err_t acquisition_history_push_payload(const uint8_t *payload, uint16_t frame_count,
                                           uint32_t frame_rate_hz);

/** @brief Obtém uma fotografia atômica do estado do ring buffer. */
bool acquisition_history_get_snapshot(acquisition_history_snapshot_t *out_snapshot);

/** @brief Lê uma amostra pela posição visual relativa ao snapshot. */
uint16_t acquisition_history_get_sample(uint8_t channel, uint32_t visual_index,
                                        const acquisition_history_snapshot_t *snapshot);

/** @brief Publica que a STM32 concluiu um ciclo finito de PWM. */
void acquisition_history_mark_cycle_done(void);

/** @brief Consome a notificação pendente de ciclo concluído. */
bool acquisition_history_take_cycle_done(void);

#ifdef __cplusplus
}
#endif
