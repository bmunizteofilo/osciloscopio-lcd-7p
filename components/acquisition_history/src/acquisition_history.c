#include "acquisition_history.h"

#include "esp_check.h"
#include "esp_heap_caps.h"

/** @brief Tag dos registros do histórico de aquisição. */
static const char *TAG = "acq_history";

/** @brief Histórico bruto; somente o Core 0 o escreve. */
static uint16_t *s_samples[4];
/** @brief Estado publicado ao leitor após os dados estarem gravados. */
static uint32_t s_sample_count;
static uint32_t s_sample_head;
static uint32_t s_generation;
static uint32_t s_frame_rate_hz;
/** @brief Notificação unidirecional do Core 0 para a interface. */
static bool s_cycle_done;

/** @brief Reserva os quatro buffers de histórico na PSRAM. */
esp_err_t acquisition_history_init(void)
{
    if (s_samples[0] != NULL) {
        return ESP_OK;
    }
    for (uint8_t channel = 0; channel < 4U; channel++) {
        s_samples[channel] = heap_caps_malloc(500U * 1024U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_samples[channel] == NULL) {
            for (uint8_t allocated = 0; allocated < channel; allocated++) {
                heap_caps_free(s_samples[allocated]);
                s_samples[allocated] = NULL;
            }
            return ESP_ERR_NO_MEM;
        }
    }
    acquisition_history_reset();
    return ESP_OK;
}

/** @brief Descarta todo o histórico e inicia uma nova geração de amostras. */
void acquisition_history_reset(void)
{
    __atomic_store_n(&s_sample_head, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&s_sample_count, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&s_frame_rate_hz, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&s_cycle_done, false, __ATOMIC_RELEASE);
    __atomic_add_fetch(&s_generation, 1U, __ATOMIC_RELEASE);
}

/** @brief Insere um bloco ADC intercalado CH1..CH4; chamada somente pelo produtor SPI. */
esp_err_t acquisition_history_push_payload(const uint8_t *payload, uint16_t frame_count,
                                           uint32_t frame_rate_hz)
{
    ESP_RETURN_ON_FALSE(payload != NULL && frame_count > 0U && frame_rate_hz > 0U,
                        ESP_ERR_INVALID_ARG, TAG, "bloco ADC invalido");
    ESP_RETURN_ON_FALSE(s_samples[0] != NULL, ESP_ERR_INVALID_STATE, TAG, "historico nao inicializado");

    uint32_t count = __atomic_load_n(&s_sample_count, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&s_sample_head, __ATOMIC_RELAXED);
    for (uint16_t frame = 0; frame < frame_count; frame++) {
        uint32_t write_index;
        if (count < ACQUISITION_HISTORY_SAMPLES_PER_CHANNEL) {
            write_index = (head + count) % ACQUISITION_HISTORY_SAMPLES_PER_CHANNEL;
            count++;
        } else {
            write_index = head;
            head = (head + 1U) % ACQUISITION_HISTORY_SAMPLES_PER_CHANNEL;
        }
        const uint8_t *source = &payload[(size_t)frame * 8U];
        for (uint8_t channel = 0; channel < 4U; channel++) {
            s_samples[channel][write_index] = ((uint16_t)source[channel * 2U] |
                                               ((uint16_t)source[channel * 2U + 1U] << 8U)) & 0x0fffU;
        }
    }
    __atomic_store_n(&s_sample_head, head, __ATOMIC_RELEASE);
    __atomic_store_n(&s_frame_rate_hz, frame_rate_hz, __ATOMIC_RELEASE);
    __atomic_store_n(&s_sample_count, count, __ATOMIC_RELEASE);
    return ESP_OK;
}

/** @brief Obtém uma fotografia atômica do estado do ring buffer. */
bool acquisition_history_get_snapshot(acquisition_history_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL || s_samples[0] == NULL) {
        return false;
    }
    out_snapshot->sample_count = __atomic_load_n(&s_sample_count, __ATOMIC_ACQUIRE);
    out_snapshot->sample_head = __atomic_load_n(&s_sample_head, __ATOMIC_ACQUIRE);
    out_snapshot->generation = __atomic_load_n(&s_generation, __ATOMIC_ACQUIRE);
    out_snapshot->frame_rate_hz = __atomic_load_n(&s_frame_rate_hz, __ATOMIC_ACQUIRE);
    return true;
}

/** @brief Lê uma amostra pela posição visual relativa ao snapshot. */
uint16_t acquisition_history_get_sample(uint8_t channel, uint32_t visual_index,
                                        const acquisition_history_snapshot_t *snapshot)
{
    if (channel >= 4U || snapshot == NULL || visual_index >= snapshot->sample_count || s_samples[channel] == NULL) {
        return 0U;
    }
    const uint32_t index = (snapshot->sample_head + visual_index) % ACQUISITION_HISTORY_SAMPLES_PER_CHANNEL;
    return s_samples[channel][index];
}

/** @brief Publica que a STM32 concluiu um ciclo finito de PWM. */
void acquisition_history_mark_cycle_done(void)
{
    __atomic_store_n(&s_cycle_done, true, __ATOMIC_RELEASE);
}

/** @brief Consome a notificação pendente de ciclo concluído. */
bool acquisition_history_take_cycle_done(void)
{
    return __atomic_exchange_n(&s_cycle_done, false, __ATOMIC_ACQ_REL);
}
