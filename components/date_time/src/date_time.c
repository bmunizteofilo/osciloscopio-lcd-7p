#include "date_time.h"

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "esp_sntp.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_manager.h"

#define DATE_TIME_TASK_STACK_SIZE 3072
#define DATE_TIME_TASK_PRIORITY 1
#define DATE_TIME_SYNC_PERIOD_SECONDS 3600
#define DATE_TIME_SNTP_SERVER "pool.ntp.org"
#define DATE_TIME_NVS_NAMESPACE "clock"
#define DATE_TIME_NVS_MANUAL_EPOCH "manual_epoch"

/** @brief Estado persistente do serviço de data e hora. */
typedef struct {
    bool started;
    bool sntp_started;
    bool synchronized;
    time_t last_sync_request;
} date_time_context_t;

/** @brief Contexto único do serviço de data e hora. */
static date_time_context_t s_date_time;

/** @brief Restaura a última data manual gravada quando ainda não há sincronismo SNTP. */
static void date_time_restore_manual_clock(void)
{
    nvs_handle_t handle;
    int64_t epoch = 0;
    if (nvs_open(DATE_TIME_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    const esp_err_t err = nvs_get_i64(handle, DATE_TIME_NVS_MANUAL_EPOCH, &epoch);
    nvs_close(handle);
    if (err == ESP_OK && epoch > 0) {
        const struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
        settimeofday(&tv, NULL);
    }
}

/** @brief Verifica se a hora local possui um ano plausível para uso na interface. */
static bool date_time_has_valid_clock(void)
{
    const time_t now = time(NULL);
    struct tm local_time = {0};
    localtime_r(&now, &local_time);
    return local_time.tm_year >= (2024 - 1900);
}

/** @brief Inicia ou reinicia uma solicitação SNTP quando o Wi-Fi está conectado. */
static void date_time_request_sntp_sync(void)
{
    if (s_date_time.sntp_started) {
        esp_sntp_restart();
    } else {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, DATE_TIME_SNTP_SERVER);
        esp_sntp_init();
        s_date_time.sntp_started = true;
    }
    s_date_time.last_sync_request = time(NULL);
}

/** @brief Mantém a hora local e solicita sincronizações periódicas pelo Wi-Fi. */
static void date_time_task(void *argument)
{
    (void)argument;
    setenv("TZ", "BRT3", 1);
    tzset();
    date_time_restore_manual_clock();
    for (;;) {
        wifi_manager_status_t wifi = {0};
        const bool connected = wifi_manager_get_status(&wifi) == ESP_OK && wifi.enabled && wifi.connected;
        const time_t now = time(NULL);
        if (connected && (!s_date_time.sntp_started || now - s_date_time.last_sync_request >= DATE_TIME_SYNC_PERIOD_SECONDS)) {
            date_time_request_sntp_sync();
        }
        s_date_time.synchronized = date_time_has_valid_clock();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/** @brief Inicia a task de baixa prioridade que mantém e sincroniza data e hora. */
esp_err_t date_time_start(void)
{
    if (s_date_time.started) {
        return ESP_OK;
    }
    s_date_time.started = true;
    return xTaskCreatePinnedToCore(date_time_task, "date_time", DATE_TIME_TASK_STACK_SIZE, NULL,
                                   DATE_TIME_TASK_PRIORITY, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

/** @brief Informa se o relógio já recebeu uma data válida por SNTP. */
bool date_time_is_synchronized(void)
{
    return s_date_time.synchronized;
}

/** @brief Formata a hora local atual no formato HH:MM. */
void date_time_format_time(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return;
    }
    const time_t now = time(NULL);
    struct tm local_time = {0};
    localtime_r(&now, &local_time);
    strftime(buffer, size, "%H:%M", &local_time);
}

/** @brief Formata a data local atual no formato DD/MM/AAAA. */
void date_time_format_date(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return;
    }
    const time_t now = time(NULL);
    struct tm local_time = {0};
    localtime_r(&now, &local_time);
    strftime(buffer, size, "%d/%m/%Y", &local_time);
}
/** @brief Ajusta manualmente data e hora locais. */
esp_err_t date_time_set_local(uint32_t year, uint32_t month, uint32_t day,
                              uint32_t hour, uint32_t minute)
{
    if (month == 0 || month > 12 || day == 0 || day > 31 || hour > 23 || minute > 59) {
        return ESP_ERR_INVALID_ARG;
    }
    struct tm value = {0};
    value.tm_year = (int)year - 1900;
    value.tm_mon = (int)month - 1;
    value.tm_mday = (int)day;
    value.tm_hour = (int)hour;
    value.tm_min = (int)minute;
    value.tm_isdst = -1;
    struct timeval tv = {.tv_sec = mktime(&value), .tv_usec = 0};
    if (tv.tv_sec == (time_t)-1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    nvs_handle_t handle;
    if (nvs_open(DATE_TIME_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i64(handle, DATE_TIME_NVS_MANUAL_EPOCH, (int64_t)tv.tv_sec);
        nvs_commit(handle);
        nvs_close(handle);
    }
    s_date_time.synchronized = true;
    return ESP_OK;
}
