#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t driver_gpio_num_t;

/** @brief Callback executada pela ISR de mudança de nível de um GPIO. */
typedef void (*driver_gpio_edge_callback_t)(void *argument);

#define DRIVER_GPIO_NUM_NC (-1)
#define DRIVER_GPIO_NUM_0 0
#define DRIVER_GPIO_NUM_1 1
#define DRIVER_GPIO_NUM_2 2
#define DRIVER_GPIO_NUM_3 3
#define DRIVER_GPIO_NUM_4 4
#define DRIVER_GPIO_NUM_5 5
#define DRIVER_GPIO_NUM_6 6
#define DRIVER_GPIO_NUM_7 7
#define DRIVER_GPIO_NUM_8 8
#define DRIVER_GPIO_NUM_9 9
#define DRIVER_GPIO_NUM_10 10
#define DRIVER_GPIO_NUM_11 11
#define DRIVER_GPIO_NUM_12 12
#define DRIVER_GPIO_NUM_13 13
#define DRIVER_GPIO_NUM_14 14
#define DRIVER_GPIO_NUM_15 15
#define DRIVER_GPIO_NUM_16 16
#define DRIVER_GPIO_NUM_17 17
#define DRIVER_GPIO_NUM_18 18
#define DRIVER_GPIO_NUM_19 19
#define DRIVER_GPIO_NUM_20 20
#define DRIVER_GPIO_NUM_21 21
#define DRIVER_GPIO_NUM_38 38
#define DRIVER_GPIO_NUM_39 39
#define DRIVER_GPIO_NUM_40 40
#define DRIVER_GPIO_NUM_41 41
#define DRIVER_GPIO_NUM_42 42
#define DRIVER_GPIO_NUM_45 45
#define DRIVER_GPIO_NUM_46 46
#define DRIVER_GPIO_NUM_47 47
#define DRIVER_GPIO_NUM_48 48

/**
 * @brief Converte um pino da camada de driver para numero inteiro de GPIO.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @return Numero inteiro do GPIO.
 */
int32_t driver_gpio_to_number(driver_gpio_num_t gpio);

/**
 * @brief Configura um GPIO como entrada.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_config_input(driver_gpio_num_t gpio);

/**
 * @brief Configura um GPIO de entrada com interrupção em ambas as bordas.
 *
 * A callback é executada em contexto de interrupção e deve ser curta, não
 * bloquear e usar somente APIs seguras para ISR. O nível do GPIO deve ser
 * consultado posteriormente em contexto de task.
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
                                                       bool enable_pull_down);

/**
 * @brief Configura um GPIO como saida.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_config_output(driver_gpio_num_t gpio);

/**
 * @brief Escreve o nivel logico de um GPIO configurado como saida.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @param[in] level Nivel logico desejado.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_set_level(driver_gpio_num_t gpio, bool level);

/**
 * @brief Le o nivel logico atual de um GPIO.
 *
 * @param[in] gpio Numero do GPIO na abstracao do driver.
 * @param[out] out_level Nivel logico lido.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t driver_gpio_get_level(driver_gpio_num_t gpio, bool *out_level);

#ifdef __cplusplus
}
#endif
