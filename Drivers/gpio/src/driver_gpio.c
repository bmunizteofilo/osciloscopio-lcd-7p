#include "driver_gpio.h"

#include "driver/gpio.h"
#include "esp_check.h"

static const char *TAG = "driver_gpio";

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
