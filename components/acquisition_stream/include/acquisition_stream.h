#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Quantidade máxima de frames em um bloco do perfil FAST. */
#define ACQUISITION_STREAM_MAX_FRAMES 256U
/** @brief Quantidade de bytes ADC em um frame de quatro canais. */
#define ACQUISITION_STREAM_FRAME_BYTES 8U
/** @brief Tamanho máximo do payload de um bloco ADC. */
#define ACQUISITION_STREAM_MAX_PAYLOAD_BYTES (ACQUISITION_STREAM_MAX_FRAMES * ACQUISITION_STREAM_FRAME_BYTES)

/** @brief Bloco ADC transportado da task SPI para o consumidor no core 1. */
typedef struct {
    uint8_t sequence;
    uint8_t profile;
    bool cycle_done;
    uint16_t frame_count;
    uint16_t payload_length;
    uint8_t payload[ACQUISITION_STREAM_MAX_PAYLOAD_BYTES];
} acquisition_stream_block_t;

/** @brief Parametros PWM serializados para a placa Power Control. */
typedef struct {
    uint8_t profile;
    uint16_t rpm;
    uint8_t ton_ms;
    uint16_t cycles;
    uint16_t pause_ms;
    uint8_t operation_mode;
} acquisition_stream_start_config_t;

/** @brief Comandos de controle consumidos exclusivamente pela task SPI. */
typedef enum {
    ACQUISITION_STREAM_COMMAND_START,
    ACQUISITION_STREAM_COMMAND_STOP,
} acquisition_stream_command_type_t;

/** @brief Solicitação de controle da aquisição. */
typedef struct {
    acquisition_stream_command_type_t type;
    acquisition_stream_start_config_t start_config;
    bool stop_pwm;
} acquisition_stream_command_t;

/** @brief Inicializa a fila SPSC de blocos e o canal de comandos. */
void acquisition_stream_init(void);

/** @brief Define a task SPI que deve ser acordada para comandos de controle. */
void acquisition_stream_set_spi_task(TaskHandle_t task);

/** @brief Atualiza o estado de presenca confirmado pelo handshake ALIVE. */
void acquisition_stream_set_power_control_online(bool online);

/** @brief Informa se a placa Power Control respondeu ao ultimo handshake ALIVE. */
bool acquisition_stream_is_power_control_online(void);

/** @brief Publica um bloco completo no produtor SPSC do core 0. */
bool acquisition_stream_publish(const acquisition_stream_block_t *block);

/** @brief Informa se ha espaco para o produtor publicar outro bloco SPSC. */
bool acquisition_stream_has_space(void);

/** @brief Retira o próximo bloco no consumidor SPSC do core 1. */
bool acquisition_stream_take(acquisition_stream_block_t *out_block);

/** @brief Descarta todos os blocos pendentes entre os dois cores. */
void acquisition_stream_clear(void);

/** @brief Solicita configuração PWM e início sincronizado da aquisição. */
bool acquisition_stream_request_start(const acquisition_stream_start_config_t *config);

/** @brief Solicita a parada segura da aquisição e, opcionalmente, do PWM. */
bool acquisition_stream_request_stop(bool stop_pwm);

/** @brief Retira uma solicitação de controle na task SPI. */
bool acquisition_stream_take_command(acquisition_stream_command_t *out_command);

#ifdef __cplusplus
}
#endif
