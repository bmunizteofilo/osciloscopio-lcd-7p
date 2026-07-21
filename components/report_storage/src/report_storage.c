#include "report_storage.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"

#define REPORT_STORAGE_NAMESPACE "relatorios"
#define REPORT_STORAGE_KEY_SEQUENCE "sequencia"
#define REPORT_STORAGE_KEY_COUNT "quantidade"

/** @brief Gera a chave NVS de uma posição do buffer circular. */
static void report_storage_make_key(uint32_t slot, char *key, size_t key_size)
{
    (void)snprintf(key, key_size, "r%03u", (unsigned)slot);
}

/** @brief Lê um metadado inteiro, retornando zero quando ainda não existe. */
static esp_err_t report_storage_get_u32(nvs_handle_t handle, const char *key, uint32_t *value)
{
    esp_err_t error = nvs_get_u32(handle, key, value);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        *value = 0;
        return ESP_OK;
    }
    return error;
}

/** @brief Abre o namespace de relatórios para leitura e escrita. */
static esp_err_t report_storage_open(nvs_handle_t *out_handle)
{
    return nvs_open(REPORT_STORAGE_NAMESPACE, NVS_READWRITE, out_handle);
}

/** @brief Inicializa os metadados do armazenamento circular de relatórios. */
esp_err_t report_storage_init(void)
{
    nvs_handle_t handle = 0;
    esp_err_t error = report_storage_open(&handle);
    if (error != ESP_OK) {
        return error;
    }
    uint32_t sequence = 0;
    uint32_t count = 0;
    error = report_storage_get_u32(handle, REPORT_STORAGE_KEY_SEQUENCE, &sequence);
    if (error == ESP_OK) {
        error = report_storage_get_u32(handle, REPORT_STORAGE_KEY_COUNT, &count);
    }
    if (error == ESP_OK) {
        error = nvs_set_u32(handle, REPORT_STORAGE_KEY_SEQUENCE, sequence);
    }
    if (error == ESP_OK) {
        error = nvs_set_u32(handle, REPORT_STORAGE_KEY_COUNT, count > REPORT_STORAGE_MAX_RECORDS ?
                            REPORT_STORAGE_MAX_RECORDS : count);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

/** @brief Persiste um relatório na próxima posição disponível do buffer circular. */
esp_err_t report_storage_save(const report_storage_record_t *record, uint32_t *out_sequence)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t error = report_storage_open(&handle);
    if (error != ESP_OK) {
        return error;
    }
    uint32_t sequence = 0;
    uint32_t count = 0;
    error = report_storage_get_u32(handle, REPORT_STORAGE_KEY_SEQUENCE, &sequence);
    if (error == ESP_OK) {
        error = report_storage_get_u32(handle, REPORT_STORAGE_KEY_COUNT, &count);
    }
    if (error == ESP_OK) {
        sequence++;
        if (sequence == 0) {
            sequence = 1;
        }
        report_storage_record_t stored_record = *record;
        stored_record.sequence = sequence;
        char key[6] = {0};
        report_storage_make_key((sequence - 1U) % REPORT_STORAGE_MAX_RECORDS, key, sizeof(key));
        error = nvs_set_blob(handle, key, &stored_record, sizeof(stored_record));
        if (error == ESP_OK) {
            error = nvs_set_u32(handle, REPORT_STORAGE_KEY_SEQUENCE, sequence);
        }
        if (error == ESP_OK) {
            error = nvs_set_u32(handle, REPORT_STORAGE_KEY_COUNT,
                                count < REPORT_STORAGE_MAX_RECORDS ? count + 1U : count);
        }
        if (error == ESP_OK) {
            error = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    if (error == ESP_OK && out_sequence != NULL) {
        *out_sequence = sequence;
    }
    return error;
}

/** @brief Consulta a quantidade atual de relatórios persistidos. */
esp_err_t report_storage_get_count(uint32_t *out_count)
{
    if (out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t error = report_storage_open(&handle);
    if (error == ESP_OK) {
        error = report_storage_get_u32(handle, REPORT_STORAGE_KEY_COUNT, out_count);
        nvs_close(handle);
    }
    return error;
}

/** @brief Lê um registro da lista ordenada do mais recente para o mais antigo. */
esp_err_t report_storage_get_recent(uint32_t recent_index, report_storage_record_t *out_record)
{
    if (out_record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t error = report_storage_open(&handle);
    uint32_t sequence = 0;
    uint32_t count = 0;
    if (error == ESP_OK) {
        error = report_storage_get_u32(handle, REPORT_STORAGE_KEY_SEQUENCE, &sequence);
    }
    if (error == ESP_OK) {
        error = report_storage_get_u32(handle, REPORT_STORAGE_KEY_COUNT, &count);
    }
    if (error == ESP_OK && (recent_index >= count || sequence == 0)) {
        error = ESP_ERR_NOT_FOUND;
    }
    if (error == ESP_OK) {
        const uint32_t record_sequence = sequence - recent_index;
        char key[6] = {0};
        size_t size = sizeof(*out_record);
        report_storage_make_key((record_sequence - 1U) % REPORT_STORAGE_MAX_RECORDS, key, sizeof(key));
        error = nvs_get_blob(handle, key, out_record, &size);
        if (error == ESP_OK && size != sizeof(*out_record)) {
            error = ESP_ERR_INVALID_SIZE;
        }
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return error;
}
