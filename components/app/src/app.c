#include "app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
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
#define APP_SPI_ALIVE_COMMAND 0x00
#define APP_SPI_ALIVE_MAGIC_0 0xa5
#define APP_SPI_ALIVE_MAGIC_1 0x5a
#define APP_SPI_PROTOCOL_VERSION 0x01
#define APP_SPI_ALIVE_TRANSACTION_SIZE 4
#define APP_SPI_RETRY_PERIOD_MS 5000
#define APP_SPI_SUPERVISOR_STACK_SIZE 3072
#define APP_SPI_SUPERVISOR_PRIORITY 5
#define APP_SPI_ACQUISITION_STACK_SIZE 4096
#define APP_SPI_ACQUISITION_PRIORITY 6

/** @brief Tag usada nos registros de inicialização da aplicação. */
static const char *TAG = "app";

/** @brief Contexto persistente da comunicação com a placa STM32. */
typedef struct {
    driver_spi_device_handle_t device;
    TaskHandle_t acquisition_task;
} app_spi_context_t;

/** @brief Contexto global usado exclusivamente pelas tarefas do core 0. */
static app_spi_context_t s_spi;

/**
 * @brief Inicializa o barramento I2C compartilhado pela tela de toque e pelo expansor.
 *
 * @param[out] out_bus Ponteiro que recebe o handle do barramento I2C.
 * @return ESP_OK em caso de sucesso ou erro do driver I2C.
 */
static esp_err_t app_i2c_init(driver_i2c_bus_handle_t *out_bus)
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
 * @brief Inicializa o barramento SPI reservado para a placa de aquisição.
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
 * @brief Adiciona o escravo SPI provisório da placa de aquisição.
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
 * @brief Confirma que o firmware compatível da STM32 responde no SPI.
 *
 * @param[in] device Dispositivo SPI da placa de aquisição.
 * @return @c ESP_OK se a assinatura e a versão do protocolo forem válidas.
 */
static esp_err_t app_spi_alive(driver_spi_device_handle_t device)
{
    const uint8_t tx[APP_SPI_ALIVE_TRANSACTION_SIZE] = {
        APP_SPI_ALIVE_COMMAND, APP_SPI_DUMMY_BYTE, APP_SPI_DUMMY_BYTE, APP_SPI_DUMMY_BYTE,
    };
    uint8_t rx[APP_SPI_ALIVE_TRANSACTION_SIZE] = {0};

    ESP_RETURN_ON_ERROR(driver_spi_transfer(device, tx, rx, sizeof(tx)), TAG, "falha na consulta ALIVE");
    ESP_RETURN_ON_FALSE(rx[1] == APP_SPI_ALIVE_MAGIC_0 && rx[2] == APP_SPI_ALIVE_MAGIC_1 &&
                            rx[3] == APP_SPI_PROTOCOL_VERSION,
                        ESP_ERR_NOT_FOUND, TAG, "assinatura STM32 invalida: %02X %02X %02X",
                        rx[1], rx[2], rx[3]);
    return ESP_OK;
}

/**
 * @brief Executa a futura aquisição de blocos SPI no core 0.
 *
 * @param[in] argument Não utilizado.
 */
static void app_spi_acquisition_task(void *argument)
{
    (void)argument;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Aguarda a presença da STM32 e inicia a aquisição após o handshake.
 *
 * @param[in] argument Ponteiro para o contexto SPI persistente.
 */
static void app_spi_supervisor_task(void *argument)
{
    app_spi_context_t *context = argument;
    for (;;) {
        if (app_spi_alive(context->device) == ESP_OK) {
            ESP_LOGI(TAG, "Placa STM32 detectada; iniciando aquisicao SPI");
            BaseType_t created = xTaskCreatePinnedToCore(app_spi_acquisition_task, "spi_acq",
                                                         APP_SPI_ACQUISITION_STACK_SIZE, NULL,
                                                         APP_SPI_ACQUISITION_PRIORITY,
                                                         &context->acquisition_task, 0);
            if (created == pdPASS) {
                vTaskDelete(NULL);
            }
            ESP_LOGE(TAG, "Falha ao criar a tarefa de aquisicao SPI");
        } else {
            ESP_LOGW(TAG, "Placa STM32 nao detectada; nova consulta em %u s",
                     APP_SPI_RETRY_PERIOD_MS / 1000);
        }
        vTaskDelay(pdMS_TO_TICKS(APP_SPI_RETRY_PERIOD_MS));
    }
}

/**
 * @brief Inicia a supervisão de presença da placa de aquisição no core 0.
 *
 * @param[in] device Dispositivo SPI já configurado.
 * @return @c ESP_OK ao criar a supervisão ou @c ESP_FAIL caso contrário.
 */
static esp_err_t app_spi_start_supervisor(driver_spi_device_handle_t device)
{
    s_spi.device = device;
    BaseType_t created = xTaskCreatePinnedToCore(app_spi_supervisor_task, "spi_watch",
                                                 APP_SPI_SUPERVISOR_STACK_SIZE, &s_spi,
                                                 APP_SPI_SUPERVISOR_PRIORITY, NULL, 0);
    return created == pdPASS ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Inicializa os periféricos e serviços que formam a aplicação.
 *
 * @return @c ESP_OK em caso de sucesso ou o primeiro erro encontrado.
 */
esp_err_t app_init(void)
{
    ESP_LOGI(TAG, "Inicializando WT32S3-07S");
    driver_i2c_bus_handle_t i2c_bus = NULL;
    ESP_RETURN_ON_ERROR(app_i2c_init(&i2c_bus), TAG, "falha ao inicializar I2C");

    driver_spi_bus_handle_t spi_bus = NULL;
    ESP_RETURN_ON_ERROR(app_spi_init(&spi_bus), TAG, "falha ao inicializar SPI");
    driver_spi_device_handle_t spi_device = NULL;
    ESP_RETURN_ON_ERROR(app_spi_device_init(spi_bus, &spi_device), TAG, "falha ao adicionar STM32 SPI");
    ESP_RETURN_ON_ERROR(app_spi_start_supervisor(spi_device), TAG, "falha ao iniciar supervisao SPI");

    aw9523b_handle_t io_expander = NULL;
    const aw9523b_config_t aw9523b_config = {
        .i2c_bus = i2c_bus, .i2c_address = 0x5b, .scl_speed_hz = APP_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(aw9523b_init(&aw9523b_config, &io_expander), TAG, "falha ao inicializar AW9523B");

    wt32s3_lcd_handle_t lcd = NULL;
    ESP_RETURN_ON_ERROR(wt32s3_lcd_init(io_expander, &lcd), TAG, "falha ao inicializar LCD");
    ESP_RETURN_ON_ERROR(wt32s3_lcd_set_backlight(lcd, 80), TAG, "falha ao ajustar backlight");

    gt911_touch_handle_t touch = NULL;
    const gt911_touch_config_t touch_config = {
        .i2c_bus = i2c_bus, .reset_io = io_expander, .scl_speed_hz = APP_I2C_SPEED_HZ,
        .x_max = WT32S3_LCD_H_RES, .y_max = WT32S3_LCD_V_RES,
    };
    ESP_RETURN_ON_ERROR(gt911_touch_init(&touch_config, &touch), TAG, "falha ao inicializar GT911");
    ESP_RETURN_ON_ERROR(app_lvgl_init(lcd, touch), TAG, "falha ao inicializar LVGL");
    ESP_LOGI(TAG, "Inicializacao concluida; framebuffer RGB esta na PSRAM externa");
    return ESP_OK;
}
