#include "app.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver_i2c.h"
#include "driver_gpio.h"
#include "driver_spi.h"
#include "driver/spi_master.h"
#include "app_lvgl.h"
#include "aw9523b.h"
#include "gt911_touch.h"
#include "wt32s3_lcd.h"
#include "wifi_manager.h"
#include "date_time.h"
#include "general_settings.h"
#include "report_storage.h"
#include "acquisition_stream.h"

#define APP_I2C_SCL_GPIO DRIVER_GPIO_NUM_47
#define APP_I2C_SDA_GPIO DRIVER_GPIO_NUM_48
#define APP_I2C_SPEED_HZ 400000
#define APP_SPI_MOSI_GPIO DRIVER_GPIO_NUM_41
#define APP_SPI_MISO_GPIO DRIVER_GPIO_NUM_4
#define APP_SPI_SCLK_GPIO DRIVER_GPIO_NUM_1
#define APP_SPI_CS_GPIO DRIVER_GPIO_NUM_20
#define APP_SPI_DRV_GPIO DRIVER_GPIO_NUM_2
#define APP_SPI_SYNC_GPIO DRIVER_GPIO_NUM_42
#define APP_SPI_MAX_TRANSFER_SIZE 4096
#define APP_SPI_CLOCK_HZ (10U * 1000U * 1000U)
#define APP_SPI_QUEUE_SIZE 4
#define APP_SPI_DUMMY_BYTE 0xff
#define APP_SPI_ALIVE_COMMAND 0x00
#define APP_SPI_READ_RESPONSE_COMMAND 0x80
#define APP_SPI_ALIVE_MAGIC_0 0xa5
#define APP_SPI_ALIVE_MAGIC_1 0x5a
#define APP_SPI_PROTOCOL_VERSION 0x03
#define APP_SPI_ALIVE_TRANSACTION_SIZE 4
#define APP_SPI_SYNC_TIMEOUT_MS 100
#define APP_SPI_REQUEST_SIZE 4
#define APP_SPI_RESPONSE_PREFIX_SIZE 1
#define APP_SPI_BLOCK_HEADER_SIZE 5
#define APP_SPI_MAX_BLOCK_PAYLOAD_SIZE 2048
#define APP_SPI_MAX_RESPONSE_SIZE (APP_SPI_RESPONSE_PREFIX_SIZE + APP_SPI_BLOCK_HEADER_SIZE + APP_SPI_MAX_BLOCK_PAYLOAD_SIZE)
#define APP_SPI_OPCODE_CONFIG_PROFILE 0x01
#define APP_SPI_OPCODE_START 0x02
#define APP_SPI_OPCODE_STOP 0x03
#define APP_SPI_OPCODE_READ_BLOCK 0x10
#define APP_SPI_OPCODE_PWM_CONFIG_0 0x20
#define APP_SPI_OPCODE_PWM_CONFIG_1 0x21
#define APP_SPI_OPCODE_PWM_CONFIG_2 0x22
#define APP_SPI_OPCODE_PWM_START 0x23
#define APP_SPI_OPCODE_PWM_STOP 0x24
#define APP_SPI_RESULT_OK 0x00
#define APP_SPI_RETRY_PERIOD_MS 5000
#define APP_SPI_SUPERVISOR_STACK_SIZE 3072
#define APP_SPI_SUPERVISOR_PRIORITY 5
#define APP_SPI_ACQUISITION_STACK_SIZE 4096
#define APP_SPI_ACQUISITION_PRIORITY 8

/** @brief Tag usada nos registros de inicialização da aplicação. */
static const char *TAG = "app";

/** @brief Contexto persistente da comunicação com a placa STM32. */
typedef struct {
    driver_spi_device_handle_t device;
    TaskHandle_t acquisition_task;
    volatile TaskHandle_t sync_waiter;
} app_spi_context_t;

/** @brief Contexto global usado exclusivamente pelas tarefas do core 0. */
static app_spi_context_t s_spi;

/** @brief Buffers persistentes da transação de resposta da task SPI. */
static uint8_t s_spi_response_tx[APP_SPI_MAX_RESPONSE_SIZE];
static uint8_t s_spi_response_rx[APP_SPI_MAX_RESPONSE_SIZE];

/**
 * @brief Notifica a task SPI de que a STM publicou um bloco ADC em DRV.
 *
 * @param[in] argument Não utilizado.
 */
static void IRAM_ATTR app_spi_drv_isr(void *argument)
{
    (void)argument;
    if (s_spi.acquisition_task != NULL) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_spi.acquisition_task, &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

/**
 * @brief Notifica a task aguardando uma mudança de nível no sinal SYNC.
 *
 * @param[in] argument Não utilizado.
 */
static void IRAM_ATTR app_spi_sync_isr(void *argument)
{
    (void)argument;
    TaskHandle_t waiter = s_spi.sync_waiter;
    if (waiter != NULL) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(waiter, &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

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
 * @brief Aguarda SYNC alcançar o nível solicitado pela máquina SPI da STM32.
 *
 * @param[in] expected_level Nível lógico aguardado.
 * @return ESP_OK ao observar o nível ou ESP_ERR_TIMEOUT se ele não ocorrer.
 */
static esp_err_t app_spi_wait_sync_level(bool expected_level)
{
    const TickType_t timeout = pdMS_TO_TICKS(APP_SPI_SYNC_TIMEOUT_MS);
    for (;;) {
        bool level = false;
        ESP_RETURN_ON_ERROR(driver_gpio_get_level(APP_SPI_SYNC_GPIO, &level), TAG,
                            "falha ao ler SYNC");
        if (level == expected_level) {
            return ESP_OK;
        }

        s_spi.sync_waiter = xTaskGetCurrentTaskHandle();
        (void)ulTaskNotifyTake(pdTRUE, 0);
        ESP_RETURN_ON_ERROR(driver_gpio_get_level(APP_SPI_SYNC_GPIO, &level), TAG,
                            "falha ao reler SYNC");
        if (level == expected_level) {
            s_spi.sync_waiter = NULL;
            return ESP_OK;
        }
        if (ulTaskNotifyTake(pdTRUE, timeout) == 0U) {
            s_spi.sync_waiter = NULL;
            return ESP_ERR_TIMEOUT;
        }
        s_spi.sync_waiter = NULL;
    }
}

/**
 * @brief Confirma que o firmware compatível da STM32 responde no SPI.
 *
 * @param[in] device Dispositivo SPI da placa de aquisição.
 * @return @c ESP_OK se a assinatura e a versão do protocolo forem válidas.
 */
static esp_err_t app_spi_alive(driver_spi_device_handle_t device)
{
    const uint8_t request_tx[APP_SPI_ALIVE_TRANSACTION_SIZE] = {
        APP_SPI_ALIVE_COMMAND, APP_SPI_DUMMY_BYTE, APP_SPI_DUMMY_BYTE, APP_SPI_DUMMY_BYTE,
    };
    const uint8_t response_tx[APP_SPI_ALIVE_TRANSACTION_SIZE] = {
        APP_SPI_READ_RESPONSE_COMMAND, APP_SPI_DUMMY_BYTE, APP_SPI_DUMMY_BYTE, APP_SPI_DUMMY_BYTE,
    };
    uint8_t response_rx[APP_SPI_ALIVE_TRANSACTION_SIZE] = {0};

    ESP_RETURN_ON_ERROR(driver_spi_transfer(device, request_tx, NULL, sizeof(request_tx)), TAG,
                        "falha ao enviar consulta ALIVE");
    ESP_RETURN_ON_ERROR(app_spi_wait_sync_level(true), TAG, "SYNC nao confirmou resposta ALIVE");
    ESP_RETURN_ON_ERROR(driver_spi_transfer(device, response_tx, response_rx, sizeof(response_tx)), TAG,
                        "falha ao ler resposta ALIVE");
    ESP_RETURN_ON_ERROR(app_spi_wait_sync_level(false), TAG, "SYNC nao liberou nova requisicao ALIVE");
    ESP_RETURN_ON_FALSE(response_rx[1] == APP_SPI_ALIVE_MAGIC_0 &&
                            response_rx[2] == APP_SPI_ALIVE_MAGIC_1 &&
                            response_rx[3] == APP_SPI_PROTOCOL_VERSION,
                        ESP_ERR_NOT_FOUND, TAG, "assinatura STM32 invalida: %02X %02X %02X",
                        response_rx[1], response_rx[2], response_rx[3]);
    return ESP_OK;
}

/**
 * @brief Executa as fases REQUEST e RESPONSE de um comando SPI.
 *
 * @param[in] device Dispositivo SPI da STM32.
 * @param[in] opcode Opcode da requisição.
 * @param[in] argument_0 Primeiro argumento.
 * @param[in] argument_1 Segundo argumento.
 * @param[in] argument_2 Terceiro argumento.
 * @param[in] response_data_length Quantidade esperada de bytes úteis na resposta.
 * @return ESP_OK em sucesso ou erro de comunicação/validação.
 */
static esp_err_t app_spi_execute_command(driver_spi_device_handle_t device, uint8_t opcode,
                                         uint8_t argument_0, uint8_t argument_1,
                                         uint8_t argument_2, size_t response_data_length)
{
    ESP_RETURN_ON_FALSE(response_data_length + APP_SPI_RESPONSE_PREFIX_SIZE <= APP_SPI_MAX_RESPONSE_SIZE,
                        ESP_ERR_INVALID_SIZE, TAG, "resposta SPI grande demais");

    const uint8_t request_tx[APP_SPI_REQUEST_SIZE] = {opcode, argument_0, argument_1, argument_2};
    const size_t response_length = APP_SPI_RESPONSE_PREFIX_SIZE + response_data_length;
    memset(s_spi_response_tx, APP_SPI_DUMMY_BYTE, response_length);
    s_spi_response_tx[0] = APP_SPI_READ_RESPONSE_COMMAND;
    memset(s_spi_response_rx, 0, response_length);

    ESP_RETURN_ON_ERROR(driver_spi_transfer(device, request_tx, NULL, sizeof(request_tx)), TAG,
                        "falha ao enviar comando SPI");
    ESP_RETURN_ON_ERROR(app_spi_wait_sync_level(true), TAG, "SYNC nao confirmou resposta SPI");
    ESP_RETURN_ON_ERROR(driver_spi_transfer(device, s_spi_response_tx, s_spi_response_rx, response_length), TAG,
                        "falha ao ler resposta SPI");
    ESP_RETURN_ON_ERROR(app_spi_wait_sync_level(false), TAG, "SYNC nao liberou nova requisicao SPI");
    return ESP_OK;
}

/**
 * @brief Configura e inicia a aquisição ADC da STM32 para o osciloscópio.
 *
 * @param[in] device Dispositivo SPI da STM32.
 * @return ESP_OK se a STM confirmou configuração e início.
 */
static esp_err_t app_spi_start_acquisition(driver_spi_device_handle_t device, uint8_t profile)
{
    ESP_RETURN_ON_ERROR(app_spi_execute_command(device, APP_SPI_OPCODE_CONFIG_PROFILE,
                                                profile, 0, 0, 1),
                        TAG, "falha ao configurar perfil SPI");
    ESP_RETURN_ON_FALSE(s_spi_response_rx[1] == APP_SPI_RESULT_OK, ESP_FAIL, TAG,
                        "STM recusou perfil SPI: %02X", s_spi_response_rx[1]);

    ESP_RETURN_ON_ERROR(app_spi_execute_command(device, APP_SPI_OPCODE_START, 0, 0, 0, 1), TAG,
                        "falha ao iniciar aquisicao SPI");
    ESP_RETURN_ON_FALSE(s_spi_response_rx[1] == APP_SPI_RESULT_OK, ESP_FAIL, TAG,
                        "STM recusou inicio SPI: %02X", s_spi_response_rx[1]);
    return ESP_OK;
}

/**
 * @brief Lê um bloco ADC pronto e registra apenas seus metadados.
 *
 * @param[in] device Dispositivo SPI da STM32.
 * @param[out] out_sequence Sequência do bloco recebido.
 * @return ESP_OK em sucesso ou erro de protocolo.
 */
static esp_err_t app_spi_stop_acquisition(driver_spi_device_handle_t device)
{
    ESP_RETURN_ON_ERROR(app_spi_execute_command(device, APP_SPI_OPCODE_STOP, 0, 0, 0, 1), TAG,
                        "falha ao parar aquisicao SPI");
    ESP_RETURN_ON_FALSE(s_spi_response_rx[1] == APP_SPI_RESULT_OK, ESP_FAIL, TAG,
                        "STM recusou parada SPI: %02X", s_spi_response_rx[1]);
    return ESP_OK;
}

/**
 * @brief Valida os limites elétricos e de serialização dos quatro bicos.
 *
 * @param[in] config Parâmetros PWM recebidos da interface.
 * @return ESP_OK se os parâmetros puderem ser enviados à STM32.
 */
static esp_err_t app_spi_validate_pwm_config(const acquisition_stream_start_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "configuracao PWM nula");
    ESP_RETURN_ON_FALSE(config->rpm >= 1U && config->rpm <= 10000U, ESP_ERR_INVALID_ARG, TAG,
                        "RPM PWM invalido: %u", (unsigned)config->rpm);
    ESP_RETURN_ON_FALSE(config->ton_ms >= 1U && config->ton_ms <= 35U, ESP_ERR_INVALID_ARG, TAG,
                        "Ton PWM invalido: %u", (unsigned)config->ton_ms);
    const uint32_t maximum_ton_us = 30000000U / config->rpm;
    ESP_RETURN_ON_FALSE((uint32_t)config->ton_ms * 1000U <= maximum_ton_us, ESP_ERR_INVALID_ARG, TAG,
                        "Ton PWM excede limite serial: ton=%u ms rpm=%u", (unsigned)config->ton_ms,
                        (unsigned)config->rpm);
    ESP_RETURN_ON_FALSE(config->pause_ms >= 100U && config->pause_ms <= 10000U, ESP_ERR_INVALID_ARG, TAG,
                        "pausa PWM invalida: %u ms", (unsigned)config->pause_ms);
    if (config->operation_mode == 0U) {
        ESP_RETURN_ON_FALSE(config->cycles >= 1U, ESP_ERR_INVALID_ARG, TAG,
                            "contador PWM manual invalido");
    } else {
        ESP_RETURN_ON_FALSE(config->cycles >= 1U && config->cycles <= 10000U, ESP_ERR_INVALID_ARG, TAG,
                            "ciclos PWM invalidos: %u", (unsigned)config->cycles);
    }
    return ESP_OK;
}

/**
 * @brief Envia uma configuração PWM em uma transação e valida sua resposta.
 */
static esp_err_t app_spi_send_pwm_config(driver_spi_device_handle_t device, uint8_t opcode,
                                         uint8_t argument_0, uint8_t argument_1, uint8_t argument_2)
{
    ESP_RETURN_ON_ERROR(app_spi_execute_command(device, opcode, argument_0, argument_1, argument_2, 1), TAG,
                        "falha ao configurar PWM");
    ESP_RETURN_ON_FALSE(s_spi_response_rx[1] == APP_SPI_RESULT_OK, ESP_FAIL, TAG,
                        "STM recusou configuracao PWM %02X: %02X", opcode, s_spi_response_rx[1]);
    return ESP_OK;
}

/**
 * @brief Configura os três registros PWM e inicia a rotina de bicos na STM32.
 */
static esp_err_t app_spi_start_pwm(driver_spi_device_handle_t device,
                                   const acquisition_stream_start_config_t *config)
{
    ESP_RETURN_ON_ERROR(app_spi_validate_pwm_config(config), TAG, "parametros PWM invalidos");
    ESP_RETURN_ON_ERROR(app_spi_send_pwm_config(device, APP_SPI_OPCODE_PWM_CONFIG_0,
                                                (uint8_t)(config->rpm & 0xffU),
                                                (uint8_t)(config->rpm >> 8U), config->ton_ms), TAG,
                        "falha no PWM_CONFIG_0");
    ESP_RETURN_ON_ERROR(app_spi_send_pwm_config(device, APP_SPI_OPCODE_PWM_CONFIG_1,
                                                (uint8_t)(config->cycles & 0xffU),
                                                (uint8_t)(config->cycles >> 8U),
                                                (uint8_t)(config->pause_ms & 0xffU)), TAG,
                        "falha no PWM_CONFIG_1");
    ESP_RETURN_ON_ERROR(app_spi_send_pwm_config(device, APP_SPI_OPCODE_PWM_CONFIG_2,
                                                (uint8_t)(config->pause_ms >> 8U),
                                                config->operation_mode, 0), TAG,
                        "falha no PWM_CONFIG_2");
    ESP_RETURN_ON_ERROR(app_spi_execute_command(device, APP_SPI_OPCODE_PWM_START, 0, 0, 0, 1), TAG,
                        "falha ao iniciar PWM");
    ESP_RETURN_ON_FALSE(s_spi_response_rx[1] == APP_SPI_RESULT_OK, ESP_FAIL, TAG,
                        "STM recusou inicio PWM: %02X", s_spi_response_rx[1]);
    return ESP_OK;
}

/** @brief Para imediatamente a rotina PWM dos bicos na STM32. */
static esp_err_t app_spi_stop_pwm(driver_spi_device_handle_t device)
{
    ESP_RETURN_ON_ERROR(app_spi_execute_command(device, APP_SPI_OPCODE_PWM_STOP, 0, 0, 0, 1), TAG,
                        "falha ao parar PWM");
    ESP_RETURN_ON_FALSE(s_spi_response_rx[1] == APP_SPI_RESULT_OK, ESP_FAIL, TAG,
                        "STM recusou parada PWM: %02X", s_spi_response_rx[1]);
    return ESP_OK;
}

/**
 * @brief Lê um bloco ADC pronto e o publica para o consumidor no core 1.
 *
 * @param[in] device Dispositivo SPI da STM32.
 * @param[in] expected_profile Perfil que determinou o tamanho esperado do bloco.
 * @return ESP_OK em sucesso ou erro de protocolo.
 */
static esp_err_t app_spi_read_block(driver_spi_device_handle_t device, uint8_t expected_profile)
{
    static const uint16_t frames_per_profile[] = {256U, 128U, 64U, 16U};
    ESP_RETURN_ON_FALSE(expected_profile < 4U, ESP_ERR_INVALID_ARG, TAG, "perfil SPI invalido");
    const uint16_t expected_frames = frames_per_profile[expected_profile];
    ESP_RETURN_ON_ERROR(app_spi_execute_command(device, APP_SPI_OPCODE_READ_BLOCK, 0, 0, 0,
                                                APP_SPI_BLOCK_HEADER_SIZE + expected_frames * 8U),
                        TAG, "falha ao solicitar bloco SPI");

    const uint8_t sequence = s_spi_response_rx[1];
    const uint8_t profile = s_spi_response_rx[2];
    const uint16_t frame_count = (uint16_t)s_spi_response_rx[3] |
                                 ((uint16_t)s_spi_response_rx[4] << 8U);
    const size_t payload_length = (size_t)frame_count * 8U;
    ESP_RETURN_ON_FALSE(profile == expected_profile && frame_count == expected_frames &&
                            payload_length <= APP_SPI_MAX_BLOCK_PAYLOAD_SIZE,
                        ESP_ERR_INVALID_RESPONSE, TAG, "cabecalho de bloco invalido");
    acquisition_stream_block_t block = {
        .sequence = sequence,
        .profile = profile,
        .frame_count = frame_count,
        .payload_length = (uint16_t)payload_length,
    };
    block.cycle_done = s_spi_response_rx[5] == 1U;
    memcpy(block.payload, &s_spi_response_rx[6], payload_length);
    ESP_RETURN_ON_FALSE(acquisition_stream_publish(&block), ESP_ERR_NO_MEM, TAG,
                        "fila de blocos SPI cheia");
    return ESP_OK;
}

/**
 * @brief Executa a futura aquisição de blocos SPI no core 0.
 *
 * @param[in] argument Não utilizado.
 */
static void app_spi_acquisition_task(void *argument)
{
    app_spi_context_t *context = argument;
    if (driver_gpio_config_input_any_edge_interrupt(APP_SPI_DRV_GPIO, app_spi_drv_isr, NULL, false) != ESP_OK) {
        ESP_LOGE(TAG, "falha ao configurar DRV SPI");
        vTaskDelete(NULL);
        return;
    }
    acquisition_stream_set_spi_task(xTaskGetCurrentTaskHandle());
    bool capturing = false;
    uint8_t active_profile = 0;
    for (;;) {
        acquisition_stream_command_t command;
        while (acquisition_stream_take_command(&command)) {
            if (command.type == ACQUISITION_STREAM_COMMAND_STOP) {
                if (command.stop_pwm && app_spi_stop_pwm(context->device) != ESP_OK) {
                    ESP_LOGW(TAG, "falha ao parar PWM SPI");
                }
                if (capturing && app_spi_stop_acquisition(context->device) != ESP_OK) {
                    ESP_LOGW(TAG, "falha ao parar aquisicao SPI");
                }
                capturing = false;
                acquisition_stream_clear();
            } else if (command.type == ACQUISITION_STREAM_COMMAND_START && command.start_config.profile < 4U) {
                if (capturing) {
                    (void)app_spi_stop_acquisition(context->device);
                }
                acquisition_stream_clear();
                if (app_spi_start_acquisition(context->device, command.start_config.profile) == ESP_OK &&
                    app_spi_start_pwm(context->device, &command.start_config) == ESP_OK) {
                    active_profile = command.start_config.profile;
                    capturing = true;
                    ESP_LOGI(TAG, "Aquisicao SPI iniciada no perfil %u", active_profile);
                } else {
                    capturing = false;
                    (void)app_spi_stop_acquisition(context->device);
                    ESP_LOGE(TAG, "falha ao iniciar aquisicao SPI");
                }
            }
        }
        bool data_ready = false;
        if (capturing && acquisition_stream_has_space() &&
            driver_gpio_get_level(APP_SPI_DRV_GPIO, &data_ready) == ESP_OK && data_ready) {
            if (app_spi_read_block(context->device, active_profile) != ESP_OK) {
                ESP_LOGW(TAG, "falha ao ler bloco SPI");
            }
            continue;
        }
        (void)ulTaskNotifyTake(pdTRUE, capturing ? pdMS_TO_TICKS(1) : portMAX_DELAY);
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
            acquisition_stream_set_power_control_online(true);
            ESP_LOGI(TAG, "Placa STM32 detectada; aguardando comando de aquisicao");
            BaseType_t created = xTaskCreatePinnedToCore(app_spi_acquisition_task, "spi_acq",
                                                         APP_SPI_ACQUISITION_STACK_SIZE, context,
                                                         APP_SPI_ACQUISITION_PRIORITY,
                                                         &context->acquisition_task, 0);
            if (created == pdPASS) {
                vTaskDelete(NULL);
            }
            ESP_LOGE(TAG, "Falha ao criar a tarefa de aquisicao SPI");
        } else {
            acquisition_stream_set_power_control_online(false);
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
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "falha ao recuperar NVS");
        nvs_err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(nvs_err, TAG, "falha ao inicializar NVS");
    acquisition_stream_init();
    ESP_RETURN_ON_ERROR(report_storage_init(), TAG, "falha ao iniciar armazenamento de relatorios");
    driver_i2c_bus_handle_t i2c_bus = NULL;
    ESP_RETURN_ON_ERROR(app_i2c_init(&i2c_bus), TAG, "falha ao inicializar I2C");

    driver_spi_bus_handle_t spi_bus = NULL;
    ESP_RETURN_ON_ERROR(app_spi_init(&spi_bus), TAG, "falha ao inicializar SPI");
    driver_spi_device_handle_t spi_device = NULL;
    ESP_RETURN_ON_ERROR(app_spi_device_init(spi_bus, &spi_device), TAG, "falha ao adicionar STM32 SPI");
    ESP_RETURN_ON_ERROR(driver_gpio_config_input_any_edge_interrupt(APP_SPI_SYNC_GPIO, app_spi_sync_isr, NULL, true), TAG,
                        "falha ao configurar SYNC SPI");
    ESP_RETURN_ON_ERROR(app_spi_start_supervisor(spi_device), TAG, "falha ao iniciar supervisao SPI");

    aw9523b_handle_t io_expander = NULL;
    const aw9523b_config_t aw9523b_config = {
        .i2c_bus = i2c_bus, .i2c_address = 0x5b, .scl_speed_hz = APP_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(aw9523b_init(&aw9523b_config, &io_expander), TAG, "falha ao inicializar AW9523B");

    wt32s3_lcd_handle_t lcd = NULL;
    ESP_RETURN_ON_ERROR(wt32s3_lcd_init(io_expander, &lcd), TAG, "falha ao inicializar LCD");
    ESP_RETURN_ON_ERROR(wt32s3_lcd_set_backlight(lcd, 80), TAG, "falha ao ajustar backlight");
    ESP_RETURN_ON_ERROR(general_settings_init(lcd), TAG, "falha ao iniciar parametros gerais");

    gt911_touch_handle_t touch = NULL;
    const gt911_touch_config_t touch_config = {
        .i2c_bus = i2c_bus, .reset_io = io_expander, .scl_speed_hz = APP_I2C_SPEED_HZ,
        .x_max = WT32S3_LCD_H_RES, .y_max = WT32S3_LCD_V_RES,
    };
    ESP_RETURN_ON_ERROR(gt911_touch_init(&touch_config, &touch), TAG, "falha ao inicializar GT911");
    ESP_RETURN_ON_ERROR(app_lvgl_init(lcd, touch), TAG, "falha ao inicializar LVGL");
    ESP_RETURN_ON_ERROR(wifi_manager_start(), TAG, "falha ao iniciar gerenciador Wi-Fi");
    ESP_RETURN_ON_ERROR(date_time_start(), TAG, "falha ao iniciar servico de data e hora");
    ESP_LOGI(TAG, "Inicializacao concluida; framebuffer RGB esta na PSRAM externa");
    return ESP_OK;
}
