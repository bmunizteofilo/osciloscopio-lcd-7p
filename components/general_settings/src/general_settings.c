#include "general_settings.h"

#include "nvs.h"

#define GENERAL_SETTINGS_NVS_NAMESPACE "general_cfg"
#define GENERAL_SETTINGS_NVS_BRIGHTNESS "brightness"
#define GENERAL_SETTINGS_NVS_VOLUME "volume"
#define GENERAL_SETTINGS_NVS_PRESSURE "pressure"
/** @brief Estado persistente dos parâmetros gerais. */
typedef struct {
    wt32s3_lcd_handle_t lcd;
    uint8_t brightness;
    general_settings_volume_t volume;
    general_settings_pressure_unit_t pressure_unit;
} general_settings_context_t;
/** @brief Contexto único dos parâmetros gerais. */
static general_settings_context_t s_settings = {
    .brightness = 80,
    .volume = GENERAL_SETTINGS_VOLUME_MEDIUM,
    .pressure_unit = GENERAL_SETTINGS_PRESSURE_BAR
};

/** @brief Salva os parâmetros gerais atuais na memória não volátil. */
static void general_settings_save(void)
{
    nvs_handle_t handle;
    if (nvs_open(GENERAL_SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_u8(handle, GENERAL_SETTINGS_NVS_BRIGHTNESS, s_settings.brightness);
    nvs_set_u8(handle, GENERAL_SETTINGS_NVS_VOLUME, (uint8_t)s_settings.volume);
    nvs_set_u8(handle, GENERAL_SETTINGS_NVS_PRESSURE, (uint8_t)s_settings.pressure_unit);
    nvs_commit(handle);
    nvs_close(handle);
}

/** @brief Recupera os parâmetros gerais previamente gravados, quando existirem. */
static void general_settings_load(void)
{
    nvs_handle_t handle;
    uint8_t value = 0;
    if (nvs_open(GENERAL_SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    if (nvs_get_u8(handle, GENERAL_SETTINGS_NVS_BRIGHTNESS, &value) == ESP_OK && value >= 10 && value <= 100) {
        s_settings.brightness = value;
    }
    if (nvs_get_u8(handle, GENERAL_SETTINGS_NVS_VOLUME, &value) == ESP_OK && value <= GENERAL_SETTINGS_VOLUME_HIGH) {
        s_settings.volume = (general_settings_volume_t)value;
    }
    if (nvs_get_u8(handle, GENERAL_SETTINGS_NVS_PRESSURE, &value) == ESP_OK && value <= GENERAL_SETTINGS_PRESSURE_PSI) {
        s_settings.pressure_unit = (general_settings_pressure_unit_t)value;
    }
    nvs_close(handle);
}
/** @brief Inicializa os parâmetros gerais vinculados ao LCD. */
esp_err_t general_settings_init(wt32s3_lcd_handle_t lcd) { if (lcd == NULL) return ESP_ERR_INVALID_ARG; s_settings.lcd = lcd; general_settings_load(); return wt32s3_lcd_set_backlight(lcd, s_settings.brightness); }
/** @brief Ajusta o brilho entre 10 e 100 por cento. */
esp_err_t general_settings_set_brightness(uint8_t percent) { if (s_settings.lcd == NULL) return ESP_ERR_INVALID_STATE; if (percent < 10) percent = 10; if (percent > 100) percent = 100; s_settings.brightness = percent; esp_err_t err = wt32s3_lcd_set_backlight(s_settings.lcd, percent); if (err == ESP_OK) general_settings_save(); return err; }
/** @brief Retorna o brilho configurado. */
uint8_t general_settings_get_brightness(void) { return s_settings.brightness; }
/** @brief Ajusta o nível de volume e beep. */
void general_settings_set_volume(general_settings_volume_t volume) { if (volume <= GENERAL_SETTINGS_VOLUME_HIGH) { s_settings.volume = volume; general_settings_save(); } }
/** @brief Retorna o nível de volume e beep. */
general_settings_volume_t general_settings_get_volume(void) { return s_settings.volume; }
/** @brief Ajusta a unidade usada para apresentar pressão. */
void general_settings_set_pressure_unit(general_settings_pressure_unit_t unit) { if (unit <= GENERAL_SETTINGS_PRESSURE_PSI) { s_settings.pressure_unit = unit; general_settings_save(); } }
/** @brief Retorna a unidade usada para apresentar pressão. */
general_settings_pressure_unit_t general_settings_get_pressure_unit(void) { return s_settings.pressure_unit; }
/** @brief Retorna o idioma disponível atualmente. */
const char *general_settings_get_language(void) { return "Portugues"; }
