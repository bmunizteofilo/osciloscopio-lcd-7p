#include "acquisition_stream.h"

#include <string.h>

#include "freertos/queue.h"

/** @brief Número de blocos de margem entre SPI e a renderização. */
#define ACQUISITION_STREAM_BLOCK_COUNT 8U
/** @brief Profundidade do canal de comandos da interface. */
#define ACQUISITION_STREAM_COMMAND_COUNT 4U

/** @brief Armazenamento SPSC de blocos recebidos pela SPI. */
static acquisition_stream_block_t s_blocks[ACQUISITION_STREAM_BLOCK_COUNT];
/** @brief Índice produzido apenas pelo core 0. */
static volatile uint32_t s_head;
/** @brief Índice consumido apenas pelo core 1. */
static volatile uint32_t s_tail;
/** @brief Fila RTOS para comandos de UI ao core SPI. */
static QueueHandle_t s_commands;
/** @brief Task SPI acordada ao chegar comando de controle. */
static TaskHandle_t s_spi_task;
/** @brief Estado publicado pelo supervisor SPI apos o handshake ALIVE. */
static volatile bool s_power_control_online;

/** @brief Inicializa o canal de dados e comandos da aquisição. */
void acquisition_stream_init(void)
{
    s_head = 0;
    s_tail = 0;
    s_spi_task = NULL;
    s_power_control_online = false;
    memset(s_blocks, 0, sizeof(s_blocks));
    if (s_commands == NULL) {
        s_commands = xQueueCreate(ACQUISITION_STREAM_COMMAND_COUNT, sizeof(acquisition_stream_command_t));
    } else {
        xQueueReset(s_commands);
    }
}

/** @brief Define a task SPI que deve ser acordada para comandos de controle. */
void acquisition_stream_set_spi_task(TaskHandle_t task)
{
    s_spi_task = task;
}

/** @brief Atualiza o estado de presenca confirmado pelo handshake ALIVE. */
void acquisition_stream_set_power_control_online(bool online)
{
    __atomic_store_n(&s_power_control_online, online, __ATOMIC_RELEASE);
}

/** @brief Informa se a placa Power Control respondeu ao ultimo handshake ALIVE. */
bool acquisition_stream_is_power_control_online(void)
{
    return __atomic_load_n(&s_power_control_online, __ATOMIC_ACQUIRE);
}

/** @brief Publica um bloco completo no produtor SPSC do core 0. */
bool acquisition_stream_publish(const acquisition_stream_block_t *block)
{
    if (block == NULL || block->payload_length > ACQUISITION_STREAM_MAX_PAYLOAD_BYTES) {
        return false;
    }
    const uint32_t head = s_head;
    const uint32_t next = (head + 1U) % ACQUISITION_STREAM_BLOCK_COUNT;
    if (next == __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE)) {
        return false;
    }
    s_blocks[head] = *block;
    __atomic_store_n(&s_head, next, __ATOMIC_RELEASE);
    return true;
}

/** @brief Informa se ha espaco para o produtor publicar outro bloco SPSC. */
bool acquisition_stream_has_space(void)
{
    const uint32_t next = (s_head + 1U) % ACQUISITION_STREAM_BLOCK_COUNT;
    return next != __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
}

/** @brief Retira o próximo bloco no consumidor SPSC do core 1. */
bool acquisition_stream_take(acquisition_stream_block_t *out_block)
{
    if (out_block == NULL) {
        return false;
    }
    const uint32_t tail = s_tail;
    if (tail == __atomic_load_n(&s_head, __ATOMIC_ACQUIRE)) {
        return false;
    }
    *out_block = s_blocks[tail];
    __atomic_store_n(&s_tail, (tail + 1U) % ACQUISITION_STREAM_BLOCK_COUNT, __ATOMIC_RELEASE);
    return true;
}

/** @brief Descarta todos os blocos pendentes entre os dois cores. */
void acquisition_stream_clear(void)
{
    __atomic_store_n(&s_tail, __atomic_load_n(&s_head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
}

/** @brief Enfileira e acorda a task SPI para uma solicitação de controle. */
static bool acquisition_stream_request(const acquisition_stream_command_t *command)
{
    if (s_commands == NULL || command == NULL) {
        return false;
    }
    if (xQueueSend(s_commands, command, 0) != pdPASS) {
        return false;
    }
    if (s_spi_task != NULL) {
        xTaskNotifyGive(s_spi_task);
    }
    return true;
}

/** @brief Solicita configuração e início da aquisição com um perfil ADC. */
bool acquisition_stream_request_start(const acquisition_stream_start_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    const acquisition_stream_command_t command = {
        .type = ACQUISITION_STREAM_COMMAND_START,
        .start_config = *config,
    };
    return acquisition_stream_request(&command);
}

/** @brief Solicita troca segura do perfil ADC sem interferir no PWM ativo. */
bool acquisition_stream_request_profile(uint8_t profile)
{
    if (profile > 3U) {
        return false;
    }
    const acquisition_stream_command_t command = {
        .type = ACQUISITION_STREAM_COMMAND_RECONFIGURE_PROFILE,
        .start_config.profile = profile,
    };
    return acquisition_stream_request(&command);
}

/** @brief Solicita a parada segura da aquisição. */
bool acquisition_stream_request_stop(bool stop_pwm)
{
    const acquisition_stream_command_t command = {
        .type = ACQUISITION_STREAM_COMMAND_STOP,
        .stop_pwm = stop_pwm,
    };
    return acquisition_stream_request(&command);
}

/** @brief Solicita pausa ou retomada do ciclo sem reinicializar seus contadores. */
bool acquisition_stream_request_cycle_paused(bool paused)
{
    const acquisition_stream_command_t command = {
        .type = paused ? ACQUISITION_STREAM_COMMAND_PAUSE_CYCLE :
                         ACQUISITION_STREAM_COMMAND_RESUME_CYCLE,
    };
    return acquisition_stream_request(&command);
}

/** @brief Retira uma solicitação de controle na task SPI. */
bool acquisition_stream_take_command(acquisition_stream_command_t *out_command)
{
    return s_commands != NULL && out_command != NULL && xQueueReceive(s_commands, out_command, 0) == pdPASS;
}
