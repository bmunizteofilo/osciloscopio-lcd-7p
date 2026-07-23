#include "driver_gpio.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_intr_alloc.h"

static const char *TAG = "driver_gpio";

/** @brief Número de GPIOs endereçáveis pelo ESP32-S3. */
#define DRIVER_GPIO_MAX_NUMBER 48

/** @brief Contexto persistente de uma entrada de evento. */
typedef struct {
    driver_gpio_num_t gpio;
    driver_gpio_edge_callback_t callback;
    void *argument;
} driver_gpio_edge_context_t;

/** @brief Contextos persistentes acessados pelas ISRs de GPIO. */
static driver_gpio_edge_context_t s_edge_contexts[DRIVER_GPIO_MAX_NUMBER + 1];

/**
 * @brief Encaminha a interrupção de mudança de nível ao cliente registrado.
 *
 * @param[in] argument Contexto persistente da entrada configurada.
 */
static void IRAM_ATTR driver_gpio_edge_isr(void *argument)
{
    driver_gpio_edge_context_t *context = argument;
    if (context != NULL && context->callback != NULL) {
        context->callback(context->argument);
    }
}

/**
 * @brief Converte um pino da camada de driver para numero inteiro de GPIO.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @return Numero inteiro do GPIO.
 */
int32_t driver_gpio_to_number(driver_gpio_num_t gpio)
{
    return gpio;
}

/**
 * @brief Configura um GPIO como entrada.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_config_input(driver_gpio_num_t gpio)
{
    ESP_RETURN_ON_FALSE(gpio != DRIVER_GPIO_NUM_NC, ESP_ERR_INVALID_ARG, TAG, "gpio invalido");

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << driver_gpio_to_number(gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&config);
}

/**
 * @brief Configura um GPIO de entrada com interrupção em ambas as bordas.
 *
 * @param[in] gpio Número do GPIO na abstração do driver.
 * @param[in] callback Rotina chamada a cada mudança de nível.
 * @param[in] argument Contexto entregue à rotina @p callback.
 * @param[in] enable_pull_down @c true para manter a entrada em nível baixo quando flutuante.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_config_input_any_edge_interrupt(driver_gpio_num_t gpio,
                                                       driver_gpio_edge_callback_t callback,
                                                       void *argument,
                                                       bool enable_pull_down)
{
    ESP_RETURN_ON_FALSE(gpio != DRIVER_GPIO_NUM_NC, ESP_ERR_INVALID_ARG, TAG, "gpio invalido");
    ESP_RETURN_ON_FALSE(callback != NULL, ESP_ERR_INVALID_ARG, TAG, "callback invalida");
    ESP_RETURN_ON_FALSE(gpio >= 0 && gpio <= DRIVER_GPIO_MAX_NUMBER, ESP_ERR_INVALID_ARG, TAG,
                        "gpio fora da faixa");

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << driver_gpio_to_number(gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = enable_pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "falha ao configurar interrupcao GPIO");

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    driver_gpio_edge_context_t *context = &s_edge_contexts[gpio];
    context->gpio = gpio;
    context->callback = callback;
    context->argument = argument;
    return gpio_isr_handler_add((gpio_num_t)driver_gpio_to_number(gpio), driver_gpio_edge_isr, context);
}

/**
 * @brief Configura um GPIO como saida.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_config_output(driver_gpio_num_t gpio)
{
    ESP_RETURN_ON_FALSE(gpio != DRIVER_GPIO_NUM_NC, ESP_ERR_INVALID_ARG, TAG, "gpio invalido");

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << driver_gpio_to_number(gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&config);
}

/**
 * @brief Escreve o nivel logico de um GPIO configurado como saida.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @param[in] level Nivel logico desejado.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_set_level(driver_gpio_num_t gpio, bool level)
{
    ESP_RETURN_ON_FALSE(gpio != DRIVER_GPIO_NUM_NC, ESP_ERR_INVALID_ARG, TAG, "gpio invalido");

    return gpio_set_level((gpio_num_t)driver_gpio_to_number(gpio), level ? 1 : 0);
}

/**
 * @brief Le o nivel logico atual de um GPIO.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @param[out] out_level Nivel logico lido.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_get_level(driver_gpio_num_t gpio, bool *out_level)
{
    ESP_RETURN_ON_FALSE(gpio != DRIVER_GPIO_NUM_NC, ESP_ERR_INVALID_ARG, TAG, "gpio invalido");
    ESP_RETURN_ON_FALSE(out_level != NULL, ESP_ERR_INVALID_ARG, TAG, "out_level invalido");

    *out_level = gpio_get_level((gpio_num_t)driver_gpio_to_number(gpio)) != 0;

    return ESP_OK;
}
