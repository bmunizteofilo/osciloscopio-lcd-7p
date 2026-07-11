#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver_gpio.h"
#include "driver_i2c.h"
#include "driver_spi.h"
#include "driver/spi_master.h"
#include "app_lvgl.h"
#include "aw9523b.h"
#include "gt911_touch.h"
#include "wt32s3_lcd.h"

#define APP_I2C_SCL_GPIO DRIVER_GPIO_NUM_47
#define APP_I2C_SDA_GPIO DRIVER_GPIO_NUM_48
#define APP_I2C_SPEED_HZ 400000
#define APP_SPI_MOSI_GPIO DRIVER_GPIO_NUM_1
#define APP_SPI_MISO_GPIO DRIVER_GPIO_NUM_2
#define APP_SPI_SCLK_GPIO DRIVER_GPIO_NUM_4
#define APP_SPI_CS_GPIO DRIVER_GPIO_NUM_19
#define APP_SPI_MAX_TRANSFER_SIZE 4096
#define APP_SPI_CLOCK_HZ (10U * 1000U * 1000U)
#define APP_SPI_QUEUE_SIZE 4
#define APP_SPI_DUMMY_BYTE 0xff

static const char *TAG = "app";

/**
 * @brief Inicializa o barramento I2C compartilhado pela tela de toque e pelo expansor.
 *
 * @param[out] out_bus Ponteiro que recebe o handle do barramento I2C.
 * @return ESP_OK em caso de sucesso ou erro do driver I2C.
 */
static esp_err_t app_bus_init(driver_i2c_bus_handle_t *out_bus)
{
    ESP_RETURN_ON_FALSE(out_bus != NULL, ESP_ERR_INVALID_ARG, TAG, "handle I2C invalido");

    const driver_i2c_bus_config_t bus_config = {
        .port = 0,
        .sda_pin = APP_I2C_SDA_GPIO,
        .scl_pin = APP_I2C_SCL_GPIO,
        .glitch_ignore_count = 7,
        .enable_internal_pullup = true,
    };

    return driver_i2c_bus_init(&bus_config, out_bus);
}

/**
 * @brief Inicializa o barramento SPI reservado para a futura placa de aquisição.
 *
 * Os GPIOs 1, 2 e 4 são provisórios e não conflitam com os periféricos atuais
 * da WT32S3-07S. O dispositivo remoto usa provisoriamente CS no GPIO19.
 *
 * @param[out] out_bus Ponteiro que recebe o handle do barramento SPI.
 * @return ESP_OK em caso de sucesso ou erro do driver SPI.
 */
static esp_err_t app_spi_init(driver_spi_bus_handle_t *out_bus)
{
    ESP_RETURN_ON_FALSE(out_bus != NULL, ESP_ERR_INVALID_ARG, TAG, "handle SPI invalido");

    const driver_spi_bus_config_t config = {
        .host = SPI3_HOST,
        .mosi_pin = APP_SPI_MOSI_GPIO,
        .miso_pin = APP_SPI_MISO_GPIO,
        .sclk_pin = APP_SPI_SCLK_GPIO,
        .max_transfer_size = APP_SPI_MAX_TRANSFER_SIZE,
    };
    return driver_spi_bus_init(&config, out_bus);
}

/**
 * @brief Adiciona o escravo SPI provisório da futura placa de aquisição.
 *
 * @param[in] bus Handle do barramento SPI já inicializado.
 * @param[out] out_device Ponteiro que recebe o handle do dispositivo.
 * @return ESP_OK em caso de sucesso ou erro do driver SPI.
 */
static esp_err_t app_spi_device_init(driver_spi_bus_handle_t bus, driver_spi_device_handle_t *out_device)
{
    ESP_RETURN_ON_FALSE(bus != NULL && out_device != NULL, ESP_ERR_INVALID_ARG, TAG, "handle SPI invalido");

    const driver_spi_device_config_t config = {
        .cs_pin = APP_SPI_CS_GPIO,
        .clock_hz = APP_SPI_CLOCK_HZ,
        .mode = 0,
        .queue_size = APP_SPI_QUEUE_SIZE,
        .dummy_byte = APP_SPI_DUMMY_BYTE,
    };
    return driver_spi_device_add(bus, &config, out_device);
}

/**
 * @brief Ponto de entrada principal do firmware.
 *
 * Inicializa I2C, expansor AW9523B, painel RGB, backlight e touch GT911 na ordem
 * exigida pela placa WT32S3-07S.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando WT32S3-07S");

    driver_i2c_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(app_bus_init(&i2c_bus));

    driver_spi_bus_handle_t spi_bus = NULL;
    ESP_ERROR_CHECK(app_spi_init(&spi_bus));

    driver_spi_device_handle_t spi_device = NULL;
    ESP_ERROR_CHECK(app_spi_device_init(spi_bus, &spi_device));

    aw9523b_handle_t io_expander = NULL;
    const aw9523b_config_t aw9523b_config = {
        .i2c_bus = i2c_bus,
        .i2c_address = 0x5b,
        .scl_speed_hz = APP_I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(aw9523b_init(&aw9523b_config, &io_expander));

    wt32s3_lcd_handle_t lcd = NULL;
    ESP_ERROR_CHECK(wt32s3_lcd_init(io_expander, &lcd));
    ESP_ERROR_CHECK(wt32s3_lcd_set_backlight(lcd, 80));

    gt911_touch_handle_t touch = NULL;
    const gt911_touch_config_t touch_config = {
        .i2c_bus = i2c_bus,
        .reset_io = io_expander,
        .scl_speed_hz = APP_I2C_SPEED_HZ,
        .x_max = WT32S3_LCD_H_RES,
        .y_max = WT32S3_LCD_V_RES,
    };
    ESP_ERROR_CHECK(gt911_touch_init(&touch_config, &touch));
    ESP_ERROR_CHECK(app_lvgl_init(lcd, touch));

    ESP_LOGI(TAG, "Inicializacao concluida; framebuffer RGB esta na PSRAM externa");
}
