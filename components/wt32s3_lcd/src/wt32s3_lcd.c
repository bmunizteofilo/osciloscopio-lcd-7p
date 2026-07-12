#include "wt32s3_lcd.h"
#include <stdlib.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WT32S3_LCD_PIN_BL GPIO_NUM_45
#define WT32S3_LCD_PIN_D0 GPIO_NUM_17
#define WT32S3_LCD_PIN_D1 GPIO_NUM_16
#define WT32S3_LCD_PIN_D2 GPIO_NUM_15
#define WT32S3_LCD_PIN_D3 GPIO_NUM_7
#define WT32S3_LCD_PIN_D4 GPIO_NUM_6
#define WT32S3_LCD_PIN_D5 GPIO_NUM_21
#define WT32S3_LCD_PIN_D6 GPIO_NUM_0
#define WT32S3_LCD_PIN_D7 GPIO_NUM_46
#define WT32S3_LCD_PIN_D8 GPIO_NUM_3
#define WT32S3_LCD_PIN_D9 GPIO_NUM_8
#define WT32S3_LCD_PIN_D10 GPIO_NUM_18
#define WT32S3_LCD_PIN_D11 GPIO_NUM_10
#define WT32S3_LCD_PIN_D12 GPIO_NUM_11
#define WT32S3_LCD_PIN_D13 GPIO_NUM_12
#define WT32S3_LCD_PIN_D14 GPIO_NUM_13
#define WT32S3_LCD_PIN_D15 GPIO_NUM_14
#define WT32S3_LCD_PIN_PCLK GPIO_NUM_9
#define WT32S3_LCD_PIN_HSYNC GPIO_NUM_5
#define WT32S3_LCD_PIN_VSYNC GPIO_NUM_38
#define WT32S3_LCD_PIN_DE GPIO_NUM_39

#define WT32S3_LCD_RST_PORT AW9523B_PORT_1
#define WT32S3_LCD_RST_PIN 0
#define WT32S3_LCD_PIXEL_CLOCK_HZ (18 * 1000 * 1000)
#define WT32S3_LCD_BACKLIGHT_TIMER LEDC_TIMER_0
#define WT32S3_LCD_BACKLIGHT_MODE LEDC_LOW_SPEED_MODE
#define WT32S3_LCD_BACKLIGHT_CHANNEL LEDC_CHANNEL_0
#define WT32S3_LCD_BACKLIGHT_MAX_DUTY 8191
#define WT32S3_LCD_FRAME_PIXELS (WT32S3_LCD_H_RES * WT32S3_LCD_V_RES)

static const char *TAG = "wt32s3_lcd";

struct wt32s3_lcd_t {
    esp_lcd_panel_handle_t panel;
    aw9523b_handle_t io_expander;
};

/**
 * @brief Inicializa o PWM usado no backlight do LCD.
 *
 * @return ESP_OK em caso de sucesso ou codigo de erro do driver LEDC.
 */
static esp_err_t wt32s3_lcd_backlight_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = WT32S3_LCD_BACKLIGHT_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = WT32S3_LCD_BACKLIGHT_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "falha ao configurar timer LEDC");

    const ledc_channel_config_t channel_config = {
        .gpio_num = WT32S3_LCD_PIN_BL,
        .speed_mode = WT32S3_LCD_BACKLIGHT_MODE,
        .channel = WT32S3_LCD_BACKLIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = WT32S3_LCD_BACKLIGHT_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel_config);
}

/**
 * @brief Executa a sequencia de reset fisico do painel pelo AW9523B.
 *
 * @param[in] io_expander Handle do expansor AW9523B.
 * @return ESP_OK em caso de sucesso ou codigo de erro do expansor.
 */
static esp_err_t wt32s3_lcd_reset_panel(aw9523b_handle_t io_expander)
{
    ESP_RETURN_ON_ERROR(aw9523b_set_pin_output(io_expander, WT32S3_LCD_RST_PORT, WT32S3_LCD_RST_PIN, true), TAG, "falha ao configurar reset");
    ESP_RETURN_ON_ERROR(aw9523b_set_pin_level(io_expander, WT32S3_LCD_RST_PORT, WT32S3_LCD_RST_PIN, false), TAG, "falha ao baixar reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(aw9523b_set_pin_level(io_expander, WT32S3_LCD_RST_PORT, WT32S3_LCD_RST_PIN, true), TAG, "falha ao liberar reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

/**
 * @brief Cria o painel RGB com framebuffer alocado preferencialmente na PSRAM.
 *
 * @param[in] handle Handle do driver WT32S3 que recebera o painel.
 * @return ESP_OK em caso de sucesso ou codigo de erro do driver LCD.
 */
static esp_err_t wt32s3_lcd_create_rgb_panel(wt32s3_lcd_handle_t handle)
{
    const esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = WT32S3_LCD_PIXEL_CLOCK_HZ,
            .h_res = WT32S3_LCD_H_RES,
            .v_res = WT32S3_LCD_V_RES,
            .hsync_pulse_width = 48,
            .hsync_back_porch = 40,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 1,
            .vsync_back_porch = 31,
            .vsync_front_porch = 13,
            .flags.hsync_idle_low = false,
            .flags.vsync_idle_low = false,
            .flags.pclk_active_neg = false,
        },
        .data_width = 16,
        .bits_per_pixel = WT32S3_LCD_BITS_PER_PIXEL,
        .num_fbs = 2,
        .bounce_buffer_size_px = WT32S3_LCD_H_RES * 40,
        .dma_burst_size = 64,
        .hsync_gpio_num = WT32S3_LCD_PIN_HSYNC,
        .vsync_gpio_num = WT32S3_LCD_PIN_VSYNC,
        .de_gpio_num = WT32S3_LCD_PIN_DE,
        .pclk_gpio_num = WT32S3_LCD_PIN_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            WT32S3_LCD_PIN_D0,
            WT32S3_LCD_PIN_D1,
            WT32S3_LCD_PIN_D2,
            WT32S3_LCD_PIN_D3,
            WT32S3_LCD_PIN_D4,
            WT32S3_LCD_PIN_D5,
            WT32S3_LCD_PIN_D6,
            WT32S3_LCD_PIN_D7,
            WT32S3_LCD_PIN_D8,
            WT32S3_LCD_PIN_D9,
            WT32S3_LCD_PIN_D10,
            WT32S3_LCD_PIN_D11,
            WT32S3_LCD_PIN_D12,
            WT32S3_LCD_PIN_D13,
            WT32S3_LCD_PIN_D14,
            WT32S3_LCD_PIN_D15,
        },
        .flags.fb_in_psram = true,
    };

    return esp_lcd_new_rgb_panel(&panel_config, &handle->panel);
}

/**
 * @brief Inicializa o painel RGB da placa WT32S3-07S.
 *
 * @param[in] io_expander Handle do expansor AW9523B usado no reset do LCD.
 * @param[out] out_handle Ponteiro que recebe o handle do driver de LCD.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t wt32s3_lcd_init(aw9523b_handle_t io_expander, wt32s3_lcd_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(io_expander != NULL && out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");

    wt32s3_lcd_handle_t handle = calloc(1, sizeof(*handle));
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_NO_MEM, TAG, "sem memoria para handle");
    handle->io_expander = io_expander;

    esp_err_t ret = ESP_OK;
    ret = wt32s3_lcd_backlight_init();
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "falha ao inicializar backlight");
    ESP_GOTO_ON_ERROR(wt32s3_lcd_reset_panel(io_expander), fail, TAG, "falha no reset do LCD");
    ESP_GOTO_ON_ERROR(wt32s3_lcd_create_rgb_panel(handle), fail, TAG, "falha ao criar painel RGB");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(handle->panel), fail, TAG, "falha ao resetar painel RGB");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(handle->panel), fail, TAG, "falha ao inicializar painel RGB");

    *out_handle = handle;
    return ESP_OK;

fail:
    if (handle->panel != NULL) {
        esp_lcd_panel_del(handle->panel);
    }
    free(handle);
    return ret;
}

/**
 * @brief Libera os recursos usados pelo driver do LCD.
 *
 * @param[in] handle Handle retornado por wt32s3_lcd_init().
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t wt32s3_lcd_deinit(wt32s3_lcd_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    esp_err_t err = esp_lcd_panel_del(handle->panel);
    free(handle);
    return err;
}

/**
 * @brief Ajusta o brilho do backlight do display.
 *
 * @param[in] handle Handle do driver de LCD.
 * @param[in] percent Brilho em porcentagem, limitado de 0 a 100.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t wt32s3_lcd_set_backlight(wt32s3_lcd_handle_t handle, uint8_t percent)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle invalido");
    if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = ((uint32_t)percent * WT32S3_LCD_BACKLIGHT_MAX_DUTY) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(WT32S3_LCD_BACKLIGHT_MODE, WT32S3_LCD_BACKLIGHT_CHANNEL, duty), TAG, "falha ao ajustar duty");
    return ledc_update_duty(WT32S3_LCD_BACKLIGHT_MODE, WT32S3_LCD_BACKLIGHT_CHANNEL);
}

/**
 * @brief Retorna os dois framebuffers internos alocados pelo driver RGB.
 *
 * @param[in] handle Handle do driver de LCD.
 * @param[out] framebuffer_a Ponteiro que recebe o primeiro framebuffer.
 * @param[out] framebuffer_b Ponteiro que recebe o segundo framebuffer.
 * @return ESP_OK em caso de sucesso ou codigo de erro do driver RGB.
 */
esp_err_t wt32s3_lcd_get_frame_buffers(wt32s3_lcd_handle_t handle, void **framebuffer_a, void **framebuffer_b)
{
    ESP_RETURN_ON_FALSE(handle != NULL && framebuffer_a != NULL && framebuffer_b != NULL, ESP_ERR_INVALID_ARG, TAG, "argumento invalido");

    return esp_lcd_rgb_panel_get_frame_buffer(handle->panel, 2, framebuffer_a, framebuffer_b);
}

/**
 * @brief Retorna o handle nativo do painel RGB do ESP-IDF.
 *
 * @param[in] handle Handle do driver de LCD.
 * @return Handle do painel ou NULL se o argumento for invalido.
 */
esp_lcd_panel_handle_t wt32s3_lcd_get_panel(wt32s3_lcd_handle_t handle)
{
    if (handle == NULL) {
        return NULL;
    }
    return handle->panel;
}
