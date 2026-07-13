#include "ui_flow.h"

#include <string.h>
#include <time.h>
#include "esp_check.h"
#include "gui_guider.h"
#include "gg_utils.h"
#include "wifi_manager.h"
#include "bluetooth_manager.h"
#include "date_time.h"
#include "osc.h"
#include "general_settings.h"

#define UI_FLOW_SPLASH_DURATION_MS 5000U
#define UI_FLOW_SPLASH_BAR_RADIUS 15

/** @brief Identificadores dos parâmetros ajustáveis do modo manual. */
typedef enum {
    UI_FLOW_MANUAL_PRESSURE,
    UI_FLOW_MANUAL_PULSE,
    UI_FLOW_MANUAL_RPM,
    UI_FLOW_MANUAL_CYCLES,
    UI_FLOW_MANUAL_PAUSE,
    UI_FLOW_MANUAL_TEMPERATURE
} ui_flow_manual_setting_t;

/** @brief Tipos de bico disponíveis para o ciclo. */
typedef enum {
    UI_FLOW_INJECTOR_12V,
    UI_FLOW_INJECTOR_75V_GDI
} ui_flow_injector_type_t;

/** @brief Instância exigida pelo código exportado pelo NXP GUI Guider. */
gg_ui_t guider_ui;

/** @brief Contexto local do fluxo de telas inicial. */
typedef struct {
    lv_obj_t *splash_bar;
    lv_obj_t *wifi_panel;
    lv_obj_t *wifi_toggle;
    lv_obj_t *wifi_status;
    lv_obj_t *wifi_network_list;
    lv_timer_t *wifi_timer;
    lv_obj_t *wifi_connection_label;
    lv_obj_t *wifi_keyboard;
    lv_obj_t *wifi_password;
    lv_obj_t *wifi_popup;
    char wifi_selected_ssid[DRIVER_WIFI_SSID_MAX_LENGTH + 1];
    wifi_manager_status_t wifi_last_status;
    bool wifi_status_valid;
    lv_obj_t *bluetooth_panel;
    lv_obj_t *bluetooth_toggle;
    lv_obj_t *bluetooth_status;
    lv_obj_t *bluetooth_device_list;
    lv_timer_t *bluetooth_timer;
    bluetooth_manager_status_t bluetooth_last_status;
    bool bluetooth_status_valid;
    lv_timer_t *menu_clock_timer;
    bool preserve_oscilloscope_screen;
    lv_obj_t *general_panel;
    lv_obj_t *general_popup;
    lv_obj_t *about_panel;
    lv_obj_t *maintenance_panel;
    lv_obj_t *general_calendar;
    lv_obj_t *general_hour_roller;
    lv_obj_t *general_minute_roller;
    lv_calendar_date_t general_selected_date;
    lv_calendar_date_t general_highlighted_date;
    lv_obj_t *manual_popup;
    lv_obj_t *manual_popup_value;
    ui_flow_manual_setting_t manual_active_setting;
    int32_t manual_pressure;
    int32_t manual_pulse;
    int32_t manual_rpm;
    int32_t manual_cycles;
    int32_t manual_pause;
    int32_t manual_temperature;
    ui_flow_injector_type_t injector_type;
} ui_flow_context_t;

/** @brief Estado persistente da splash e de sua transição. */
static ui_flow_context_t s_ui_flow = {
    .manual_pressure = 1,
    .manual_pulse = 1,
    .manual_rpm = 500,
    .manual_cycles = 1,
    .manual_pause = 100,
    .manual_temperature = 25
};

/** @brief Anos disponíveis para seleção manual no calendário. */
static const char s_ui_flow_calendar_years[] =
    "2020\n2021\n2022\n2023\n2024\n2025\n2026\n2027\n2028\n2029\n"
    "2030\n2031\n2032\n2033\n2034\n2035";

static void ui_flow_show_main_menu(void);
static void ui_flow_show_settings(void);
static void ui_flow_show_bicos_step_one(void);
static void ui_flow_show_bicos_step_two(void);
static void ui_flow_show_bicos_config_manual(void);
static void ui_flow_show_ready_to_start(void);
static void ui_flow_destroy_wifi_panel(void);
static void ui_flow_destroy_bluetooth_panel(void);
static void ui_flow_destroy_general_panel(void);
static void ui_flow_destroy_about_panel(void);
static void ui_flow_destroy_maintenance_panel(void);
static void ui_flow_oscilloscope_menu_cb(void);
static void ui_flow_show_general_panel(void);

/** @brief Fecha o diálogo de ajuste de um parâmetro do modo manual. */
static void ui_flow_close_manual_popup(void)
{
    if (s_ui_flow.manual_popup != NULL) {
        lv_obj_delete(s_ui_flow.manual_popup);
        s_ui_flow.manual_popup = NULL;
        s_ui_flow.manual_popup_value = NULL;
    }
}

/** @brief Retorna o valor atual de um parâmetro do modo manual. */
static int32_t ui_flow_manual_get_value(ui_flow_manual_setting_t setting)
{
    switch (setting) {
    case UI_FLOW_MANUAL_PRESSURE: return s_ui_flow.manual_pressure;
    case UI_FLOW_MANUAL_PULSE: return s_ui_flow.manual_pulse;
    case UI_FLOW_MANUAL_RPM: return s_ui_flow.manual_rpm;
    case UI_FLOW_MANUAL_CYCLES: return s_ui_flow.manual_cycles;
    case UI_FLOW_MANUAL_PAUSE: return s_ui_flow.manual_pause;
    case UI_FLOW_MANUAL_TEMPERATURE: return s_ui_flow.manual_temperature;
    default: return 0;
    }
}

/** @brief Salva o valor atual de um parâmetro do modo manual. */
static void ui_flow_manual_set_value(ui_flow_manual_setting_t setting, int32_t value)
{
    switch (setting) {
    case UI_FLOW_MANUAL_PRESSURE: s_ui_flow.manual_pressure = value; break;
    case UI_FLOW_MANUAL_PULSE: s_ui_flow.manual_pulse = value; break;
    case UI_FLOW_MANUAL_RPM: s_ui_flow.manual_rpm = value; break;
    case UI_FLOW_MANUAL_CYCLES: s_ui_flow.manual_cycles = value; break;
    case UI_FLOW_MANUAL_PAUSE: s_ui_flow.manual_pause = value; break;
    case UI_FLOW_MANUAL_TEMPERATURE: s_ui_flow.manual_temperature = value; break;
    default: break;
    }
}

/** @brief Retorna o menor valor permitido para um parâmetro manual. */
static int32_t ui_flow_manual_get_min(ui_flow_manual_setting_t setting)
{
    switch (setting) {
    case UI_FLOW_MANUAL_PRESSURE:
    case UI_FLOW_MANUAL_PULSE:
    case UI_FLOW_MANUAL_CYCLES: return 1;
    case UI_FLOW_MANUAL_RPM: return 500;
    case UI_FLOW_MANUAL_PAUSE: return 100;
    case UI_FLOW_MANUAL_TEMPERATURE: return 25;
    default: return 0;
    }
}

/** @brief Retorna o maior valor permitido para um parâmetro manual. */
static int32_t ui_flow_manual_get_max(ui_flow_manual_setting_t setting)
{
    switch (setting) {
    case UI_FLOW_MANUAL_PRESSURE: return 150;
    case UI_FLOW_MANUAL_PULSE: return 35;
    case UI_FLOW_MANUAL_RPM:
    case UI_FLOW_MANUAL_CYCLES: return 10000;
    case UI_FLOW_MANUAL_PAUSE: return 10000;
    case UI_FLOW_MANUAL_TEMPERATURE: return 80;
    default: return 0;
    }
}

/** @brief Retorna o título usado pelo diálogo de um parâmetro manual. */
static const char *ui_flow_manual_get_title(ui_flow_manual_setting_t setting)
{
    static const char *const titles[] = {"Pressao", "Pulso", "RPM", "Ciclos", "Tempo de Pausa", "Temperatura"};
    return titles[setting];
}

/** @brief Formata um valor manual com sua unidade para apresentação na interface. */
static void ui_flow_manual_format_value(ui_flow_manual_setting_t setting, int32_t value, char *buffer, size_t size)
{
    static const char *const units[] = {"bar", "ms", "rpm", "ciclos", "ms", "C"};
    lv_snprintf(buffer, size, "%ld %s", (long)value, units[setting]);
}

/** @brief Formata o valor para os botões manuais. */
static void ui_flow_manual_format_button_value(ui_flow_manual_setting_t setting, int32_t value, char *buffer, size_t size)
{
    static const char *const units[] = {"bar", "ms", "rpm", "ciclos", "ms", "C"};
    lv_snprintf(buffer, size, "%ld %s", (long)value, units[setting]);
}

/** @brief Atualiza os seis labels de valor da tela de configuração manual. */
static void ui_flow_manual_update_labels(void)
{
    char value[24] = {0};
    lv_obj_t *labels[] = {
        guider_ui.screen_bicos_step_config_manual.button_pressao_label_bt_value_pressao,
        guider_ui.screen_bicos_step_config_manual.button_pulso_ms_label_bt_value_pulso,
        guider_ui.screen_bicos_step_config_manual.button_rpm_label_bt_value_rpm,
        guider_ui.screen_bicos_step_config_manual.button_ciclos_label_bt_value_ciclos,
        guider_ui.screen_bicos_step_config_manual.button_tempo_de_pausa_label_bt_value_tempo_de_pausa,
        guider_ui.screen_bicos_step_config_manual.button_temperatura_label_bt_value_temperatura
    };
    for (uint32_t setting = UI_FLOW_MANUAL_PRESSURE; setting <= UI_FLOW_MANUAL_TEMPERATURE; setting++) {
        if (labels[setting] != NULL) {
            ui_flow_manual_format_button_value((ui_flow_manual_setting_t)setting,
                                               ui_flow_manual_get_value((ui_flow_manual_setting_t)setting), value, sizeof(value));
            lv_label_set_text(labels[setting], value);
        }
    }
}

/** @brief Cria indicadores de navegação alinhados à direita dos botões manuais. */
static void ui_flow_manual_create_navigation_indicators(void)
{
    lv_obj_t *buttons[] = {
        guider_ui.screen_bicos_step_config_manual.button_pressao,
        guider_ui.screen_bicos_step_config_manual.button_pulso_ms,
        guider_ui.screen_bicos_step_config_manual.button_rpm,
        guider_ui.screen_bicos_step_config_manual.button_ciclos,
        guider_ui.screen_bicos_step_config_manual.button_tempo_de_pausa,
        guider_ui.screen_bicos_step_config_manual.button_temperatura
    };

    if (guider_ui.screen_bicos_step_config_manual.button_ciclos_label_bt_value_ciclos != NULL) {
        lv_obj_set_x(guider_ui.screen_bicos_step_config_manual.button_ciclos_label_bt_value_ciclos, 205);
    }

    for (uint32_t setting = UI_FLOW_MANUAL_PRESSURE; setting <= UI_FLOW_MANUAL_TEMPERATURE; setting++) {
        if (buttons[setting] == NULL) {
            continue;
        }
        lv_obj_t *indicator = lv_label_create(buttons[setting]);
        lv_label_set_text(indicator, ">");
        lv_obj_set_style_text_color(indicator, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(indicator, &lv_font_montserratMedium_18, LV_PART_MAIN);
        lv_obj_align(indicator, LV_ALIGN_RIGHT_MID, 9, 0);
    }
}

/** @brief Adiciona uma linha de resumo ao container da tela de confirmação. */
static void ui_flow_ready_add_info_row(lv_obj_t *parent, const char *name, const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(name_label, &lv_font_montserratMedium_18, LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(value_label, &lv_font_montserratMedium_18, LV_PART_MAIN);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

/** @brief Preenche o container com o resumo dos parâmetros que serão executados. */
static void ui_flow_ready_populate_info(void)
{
    lv_obj_t *container = guider_ui.screen_pronto_pra_iniciar.container_infos;
    if (container == NULL) {
        return;
    }
    lv_obj_clean(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_left(container, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(container, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(container, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(container, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(container, 0, LV_PART_MAIN);

    char value[24] = {0};
    ui_flow_ready_add_info_row(container, "Tipo de Bico",
                               s_ui_flow.injector_type == UI_FLOW_INJECTOR_75V_GDI ? "75V GDI" : "12V comum");
    ui_flow_manual_format_value(UI_FLOW_MANUAL_PRESSURE, s_ui_flow.manual_pressure, value, sizeof(value));
    ui_flow_ready_add_info_row(container, "Pressao", value);
    ui_flow_manual_format_value(UI_FLOW_MANUAL_PULSE, s_ui_flow.manual_pulse, value, sizeof(value));
    ui_flow_ready_add_info_row(container, "Pulso", value);
    ui_flow_manual_format_value(UI_FLOW_MANUAL_RPM, s_ui_flow.manual_rpm, value, sizeof(value));
    ui_flow_ready_add_info_row(container, "RPM", value);
    ui_flow_manual_format_value(UI_FLOW_MANUAL_CYCLES, s_ui_flow.manual_cycles, value, sizeof(value));
    ui_flow_ready_add_info_row(container, "Ciclos", value);
    ui_flow_manual_format_value(UI_FLOW_MANUAL_PAUSE, s_ui_flow.manual_pause, value, sizeof(value));
    ui_flow_ready_add_info_row(container, "Tempo de Pausa", value);
    ui_flow_manual_format_value(UI_FLOW_MANUAL_TEMPERATURE, s_ui_flow.manual_temperature, value, sizeof(value));
    ui_flow_ready_add_info_row(container, "Temperatura", value);
}

/** @brief Atualiza a prévia exibida ao mover o slider de configuração manual. */
static void ui_flow_manual_slider_cb(lv_event_t *event)
{
    char value[24] = {0};
    ui_flow_manual_format_value(s_ui_flow.manual_active_setting,
                                lv_slider_get_value(lv_event_get_target_obj(event)), value, sizeof(value));
    lv_label_set_text(s_ui_flow.manual_popup_value, value);
}

/** @brief Confirma o valor selecionado e atualiza a tela de configuração manual. */
static void ui_flow_manual_apply_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_user_data(event);
    ui_flow_manual_set_value(s_ui_flow.manual_active_setting, lv_slider_get_value(slider));
    ui_flow_manual_update_labels();
    ui_flow_close_manual_popup();
}

/** @brief Fecha o diálogo manual sem gravar o valor atualmente pré-visualizado. */
static void ui_flow_manual_cancel_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_manual_popup();
}

/** @brief Abre o diálogo de ajuste do parâmetro manual selecionado. */
static void ui_flow_manual_setting_button_cb(lv_event_t *event)
{
    const ui_flow_manual_setting_t setting = (ui_flow_manual_setting_t)(uintptr_t)lv_event_get_user_data(event);
    char value[24] = {0};
    ui_flow_close_manual_popup();
    s_ui_flow.manual_active_setting = setting;
    s_ui_flow.manual_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.manual_popup, 360, 240);
    lv_obj_center(s_ui_flow.manual_popup);
    lv_obj_set_style_bg_color(s_ui_flow.manual_popup, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.manual_popup, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.manual_popup, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui_flow.manual_popup, 8, LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(s_ui_flow.manual_popup);
    lv_label_set_text(title, ui_flow_manual_get_title(setting));
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_set_pos(title, 0, 16);
    lv_obj_t *slider = lv_slider_create(s_ui_flow.manual_popup);
    lv_obj_set_size(slider, 300, 20);
    lv_obj_set_pos(slider, 10, 78);
    lv_slider_set_range(slider, ui_flow_manual_get_min(setting), ui_flow_manual_get_max(setting));
    lv_slider_set_value(slider, ui_flow_manual_get_value(setting), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x1976d2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2196f3), LV_PART_KNOB);
    s_ui_flow.manual_popup_value = lv_label_create(s_ui_flow.manual_popup);
    ui_flow_manual_format_value(setting, ui_flow_manual_get_value(setting), value, sizeof(value));
    lv_label_set_text(s_ui_flow.manual_popup_value, value);
    lv_obj_set_style_text_color(s_ui_flow.manual_popup_value, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align_to(s_ui_flow.manual_popup_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);
    lv_obj_add_event_cb(slider, ui_flow_manual_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *apply = lv_button_create(s_ui_flow.manual_popup);
    lv_obj_set_size(apply, 110, 38);
    lv_obj_set_pos(apply, 210, 156);
    lv_obj_set_style_bg_color(apply, lv_color_hex(0x09572e), LV_PART_MAIN);
    lv_obj_set_style_border_color(apply, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(apply, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(apply, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(apply, ui_flow_manual_apply_cb, LV_EVENT_CLICKED, slider);
    lv_obj_t *apply_label = lv_label_create(apply);
    lv_label_set_text(apply_label, "Aplicar");
    lv_obj_set_style_text_color(apply_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(apply_label);
    lv_obj_t *cancel = lv_button_create(s_ui_flow.manual_popup);
    lv_obj_set_size(cancel, 110, 38);
    lv_obj_set_pos(cancel, 0, 156);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x8b1e1e), LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(cancel, ui_flow_manual_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "Cancelar");
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(cancel_label);
}

/** @brief Fecha o diálogo de edição e atualiza os valores apresentados no painel. */
static void ui_flow_general_close_popup(lv_event_t *event)
{
    if (s_ui_flow.general_popup != NULL) {
        lv_obj_delete(s_ui_flow.general_popup);
        s_ui_flow.general_popup = NULL;
        s_ui_flow.general_calendar = NULL;
        s_ui_flow.general_hour_roller = NULL;
        s_ui_flow.general_minute_roller = NULL;
    }
    if (event != NULL) {
        ui_flow_show_general_panel();
    }
}

/** @brief Aplica o brilho escolhido no slider de parâmetros gerais. */
static void ui_flow_general_popup_brightness_cb(lv_event_t *event)
{
    const uint8_t brightness = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(event));
    general_settings_set_brightness(brightness);
    lv_obj_t *value_label = lv_event_get_user_data(event);
    if (value_label != NULL) {
        lv_label_set_text_fmt(value_label, "%u%%", (unsigned)brightness);
    }
}

/** @brief Armazena a unidade de pressão selecionada no diálogo. */
static void ui_flow_general_pressure_cb(lv_event_t *event)
{
    general_settings_set_pressure_unit(lv_dropdown_get_selected(lv_event_get_target_obj(event)) == 0 ?
                                           GENERAL_SETTINGS_PRESSURE_BAR : GENERAL_SETTINGS_PRESSURE_PSI);
}

/** @brief Armazena o volume selecionado no diálogo. */
static void ui_flow_general_volume_cb(lv_event_t *event)
{
    general_settings_set_volume((general_settings_volume_t)lv_dropdown_get_selected(lv_event_get_target_obj(event)));
}

/** @brief Registra a data escolhida no calendário antes da confirmação. */
static void ui_flow_general_calendar_cb(lv_event_t *event)
{
    lv_calendar_date_t date = {0};
    lv_obj_t *calendar = (lv_obj_t *)lv_event_get_current_target(event);
    if (lv_event_get_target_obj(event) != lv_calendar_get_btnmatrix(calendar)) {
        return;
    }
    if (lv_calendar_get_pressed_date(calendar, &date) == LV_RESULT_OK) {
        s_ui_flow.general_selected_date = date;
        s_ui_flow.general_highlighted_date = date;
        lv_calendar_set_highlighted_dates(s_ui_flow.general_calendar, &s_ui_flow.general_highlighted_date, 1);
    }
}

/** @brief Confirma a data e a hora selecionadas pelo usuário. */
static void ui_flow_general_apply_date_time_cb(lv_event_t *event)
{
    (void)event;
    if (s_ui_flow.general_selected_date.year == 0 && s_ui_flow.general_calendar != NULL) {
        const lv_calendar_date_t *shown = lv_calendar_get_showed_date(s_ui_flow.general_calendar);
        s_ui_flow.general_selected_date = *shown;
    }
    if (s_ui_flow.general_selected_date.year != 0) {
        date_time_set_local(s_ui_flow.general_selected_date.year, s_ui_flow.general_selected_date.month,
                            s_ui_flow.general_selected_date.day,
                            lv_roller_get_selected(s_ui_flow.general_hour_roller),
                            lv_roller_get_selected(s_ui_flow.general_minute_roller));
    }
    ui_flow_general_close_popup(event);
}

/** @brief Aplica a aparência padronizada dos controles de parâmetros gerais. */
static void ui_flow_general_style_control(lv_obj_t *object)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(object, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 8, LV_PART_MAIN);
    lv_obj_set_style_text_color(object, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(object, &lv_font_montserratMedium_20, LV_PART_MAIN);
}

/** @brief Exibe o detalhe da opção geral selecionada. */
static void ui_flow_general_option_cb(lv_event_t *event)
{
    const uint8_t option = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    ui_flow_general_close_popup(NULL);
    s_ui_flow.general_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.general_popup, option == 1 ? 390 : 360, option == 1 ? 450 : 240); lv_obj_center(s_ui_flow.general_popup);
    lv_obj_set_style_bg_color(s_ui_flow.general_popup, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.general_popup, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.general_popup, 2, LV_PART_MAIN);
    const char *titles[] = {"Idioma", "Data e Hora", "Unidade de Pressao", "Brilho da Tela", "Volume / Beep"};
    lv_obj_t *title = lv_label_create(s_ui_flow.general_popup); lv_label_set_text(title, titles[option]);
    lv_obj_set_style_text_font(title, &lv_font_montserratMedium_20, LV_PART_MAIN); lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN); lv_obj_set_pos(title, 16, 16);
    if (option == 1) {
        const time_t now = time(NULL);
        struct tm local_time = {0};
        localtime_r(&now, &local_time);
        s_ui_flow.general_selected_date = (lv_calendar_date_t){.year = (uint16_t)(local_time.tm_year + 1900),
                                                               .month = (uint8_t)(local_time.tm_mon + 1),
                                                               .day = (uint8_t)local_time.tm_mday};
        s_ui_flow.general_highlighted_date = s_ui_flow.general_selected_date;
        s_ui_flow.general_calendar = lv_calendar_create(s_ui_flow.general_popup);
        lv_obj_set_size(s_ui_flow.general_calendar, 330, 245); lv_obj_set_pos(s_ui_flow.general_calendar, 10, 48);
        lv_calendar_set_today_date(s_ui_flow.general_calendar, s_ui_flow.general_selected_date.year,
                                   s_ui_flow.general_selected_date.month, s_ui_flow.general_selected_date.day);
        lv_calendar_set_month_shown(s_ui_flow.general_calendar, s_ui_flow.general_selected_date.year,
                                    s_ui_flow.general_selected_date.month);
        lv_calendar_set_highlighted_dates(s_ui_flow.general_calendar, &s_ui_flow.general_highlighted_date, 1);
        lv_obj_t *calendar_header = lv_calendar_add_header_dropdown(s_ui_flow.general_calendar);
        lv_calendar_header_dropdown_set_year_list(s_ui_flow.general_calendar, s_ui_flow_calendar_years);
        lv_obj_send_event(calendar_header, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(s_ui_flow.general_calendar, ui_flow_general_calendar_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_t *time_label = lv_label_create(s_ui_flow.general_popup); lv_label_set_text(time_label, "Hora"); lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), LV_PART_MAIN); lv_obj_set_pos(time_label, 32, 310);
        s_ui_flow.general_hour_roller = lv_roller_create(s_ui_flow.general_popup);
        lv_obj_set_size(s_ui_flow.general_hour_roller, 90, 58); lv_obj_set_pos(s_ui_flow.general_hour_roller, 75, 328);
        lv_roller_set_options(s_ui_flow.general_hour_roller, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", LV_ROLLER_MODE_NORMAL);
        lv_roller_set_selected(s_ui_flow.general_hour_roller, (uint32_t)local_time.tm_hour, LV_ANIM_OFF);
        lv_obj_t *minute_label = lv_label_create(s_ui_flow.general_popup); lv_label_set_text(minute_label, "Min"); lv_obj_set_style_text_color(minute_label, lv_color_hex(0xffffff), LV_PART_MAIN); lv_obj_set_pos(minute_label, 212, 310);
        s_ui_flow.general_minute_roller = lv_roller_create(s_ui_flow.general_popup);
        lv_obj_set_size(s_ui_flow.general_minute_roller, 90, 58); lv_obj_set_pos(s_ui_flow.general_minute_roller, 235, 328);
        lv_roller_set_options(s_ui_flow.general_minute_roller, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59", LV_ROLLER_MODE_NORMAL);
        lv_roller_set_selected(s_ui_flow.general_minute_roller, (uint32_t)local_time.tm_min, LV_ANIM_OFF);
        lv_obj_t *apply = lv_button_create(s_ui_flow.general_popup); lv_obj_set_size(apply, 100, 36); lv_obj_set_pos(apply, 228, 400); ui_flow_general_style_control(apply); lv_obj_add_event_cb(apply, ui_flow_general_apply_date_time_cb, LV_EVENT_CLICKED, NULL); lv_obj_t *label = lv_label_create(apply); lv_label_set_text(label, "Aplicar"); lv_obj_center(label);
        return;
    }
    if (option == 3) { lv_obj_t *slider = lv_slider_create(s_ui_flow.general_popup); lv_obj_set_size(slider, 300, 20); lv_obj_set_pos(slider, 16, 75); lv_slider_set_range(slider, 10, 100); lv_slider_set_value(slider, general_settings_get_brightness(), LV_ANIM_OFF); lv_obj_t *value_label = lv_label_create(s_ui_flow.general_popup); lv_label_set_text_fmt(value_label, "%u%%", (unsigned)general_settings_get_brightness()); lv_obj_set_style_text_color(value_label, lv_color_hex(0xffffff), LV_PART_MAIN); lv_obj_align_to(value_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 12); lv_obj_add_event_cb(slider, ui_flow_general_popup_brightness_cb, LV_EVENT_VALUE_CHANGED, value_label); }
    else { lv_obj_t *choice = lv_dropdown_create(s_ui_flow.general_popup); lv_obj_set_size(choice, 300, 42); lv_obj_set_pos(choice, 16, 75); ui_flow_general_style_control(choice); lv_dropdown_set_options(choice, option == 0 ? "Portugues" : option == 2 ? "bar\npsi" : "Baixo\nMedio\nAlto"); if (option == 2) { lv_dropdown_set_selected(choice, general_settings_get_pressure_unit()); lv_obj_add_event_cb(choice, ui_flow_general_pressure_cb, LV_EVENT_VALUE_CHANGED, NULL); } else if (option == 4) { lv_dropdown_set_selected(choice, general_settings_get_volume()); lv_obj_add_event_cb(choice, ui_flow_general_volume_cb, LV_EVENT_VALUE_CHANGED, NULL); } }
    lv_obj_t *close = lv_button_create(s_ui_flow.general_popup); lv_obj_set_size(close, 100, 36); lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -12, -12); ui_flow_general_style_control(close); lv_obj_add_event_cb(close, ui_flow_general_close_popup, LV_EVENT_CLICKED, NULL); lv_obj_t *label = lv_label_create(close); lv_label_set_text(label, "OK"); lv_obj_center(label);
}

/** @brief Cria uma linha compacta de opção no estilo da lista de parâmetros. */
static void ui_flow_general_row(lv_obj_t *parent, const char *name, const char *value, int32_t y, uint8_t option)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, 340, 50);
    lv_obj_set_pos(row, 15, y);
    ui_flow_general_style_control(row);
    lv_obj_add_event_cb(row, ui_flow_general_option_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)option);
    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(name_label, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xd0d0d0), LV_PART_MAIN);
    lv_obj_set_style_text_font(value_label, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -25, 0);
    lv_obj_t *arrow = lv_label_create(row);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_color(arrow, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
}

/** @brief Mostra os controles dos parâmetros gerais no painel direito. */
static void ui_flow_show_general_panel(void)
{
    ui_flow_destroy_wifi_panel();
    ui_flow_destroy_bluetooth_panel();
    ui_flow_destroy_about_panel();
    ui_flow_destroy_maintenance_panel();
    ui_flow_destroy_general_panel();
    s_ui_flow.general_panel = lv_obj_create(guider_ui.screen_configuracoes.screen);
    lv_obj_set_size(s_ui_flow.general_panel, 370, 376);
    lv_obj_set_pos(s_ui_flow.general_panel, 420, 21);
    lv_obj_set_style_bg_color(s_ui_flow.general_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.general_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.general_panel, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui_flow.general_panel, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui_flow.general_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui_flow.general_panel, LV_OBJ_FLAG_SCROLLABLE);
    char brightness[8] = {0};
    lv_snprintf(brightness, sizeof(brightness), "%u%%", (unsigned)general_settings_get_brightness());
    const char *volume = general_settings_get_volume() == GENERAL_SETTINGS_VOLUME_LOW ? "Baixo" :
                         general_settings_get_volume() == GENERAL_SETTINGS_VOLUME_HIGH ? "Alto" : "Medio";
    const char *pressure = general_settings_get_pressure_unit() == GENERAL_SETTINGS_PRESSURE_PSI ? "psi" : "bar";
    ui_flow_general_row(s_ui_flow.general_panel, "Idioma", general_settings_get_language(), 14, 0);
    ui_flow_general_row(s_ui_flow.general_panel, "Data e Hora", "", 70, 1);
    ui_flow_general_row(s_ui_flow.general_panel, "Unidade de Pressao", pressure, 126, 2);
    ui_flow_general_row(s_ui_flow.general_panel, "Brilho da Tela", brightness, 182, 3);
    ui_flow_general_row(s_ui_flow.general_panel, "Volume / Beep", volume, 238, 4);
}

/** @brief Abre os parâmetros gerais ao tocar na opção correspondente. */
static void ui_flow_general_button_cb(lv_event_t *event) { (void)event; ui_flow_show_general_panel(); }

/** @brief Libera o painel de parâmetros gerais antes de trocar o conteúdo direito. */
static void ui_flow_destroy_general_panel(void)
{
    ui_flow_general_close_popup(NULL);
    if (s_ui_flow.general_panel != NULL) {
        lv_obj_delete(s_ui_flow.general_panel);
        s_ui_flow.general_panel = NULL;
    }
}

/** @brief Libera o painel de informações do equipamento. */
static void ui_flow_destroy_about_panel(void)
{
    if (s_ui_flow.about_panel != NULL) {
        lv_obj_delete(s_ui_flow.about_panel);
        s_ui_flow.about_panel = NULL;
    }
}

/** @brief Cria uma linha informativa sem ação no painel Sobre o equipamento. */
static void ui_flow_about_row(lv_obj_t *parent, const char *text, int32_t y)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, 340, 50);
    lv_obj_set_pos(row, 15, y);
    ui_flow_general_style_control(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
}

/** @brief Exibe as informações estáticas da versão do equipamento. */
static void ui_flow_show_about_panel(void)
{
    ui_flow_destroy_wifi_panel();
    ui_flow_destroy_bluetooth_panel();
    ui_flow_destroy_general_panel();
    ui_flow_destroy_about_panel();
    ui_flow_destroy_maintenance_panel();
    s_ui_flow.about_panel = lv_obj_create(guider_ui.screen_configuracoes.screen);
    lv_obj_set_size(s_ui_flow.about_panel, 370, 376);
    lv_obj_set_pos(s_ui_flow.about_panel, 420, 21);
    lv_obj_set_style_bg_color(s_ui_flow.about_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.about_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.about_panel, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui_flow.about_panel, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui_flow.about_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui_flow.about_panel, LV_OBJ_FLAG_SCROLLABLE);
    ui_flow_about_row(s_ui_flow.about_panel, "MA-02 PIEZZO 6B", 14);
    ui_flow_about_row(s_ui_flow.about_panel, "Firmware: 1.0.0", 70);
    ui_flow_about_row(s_ui_flow.about_panel, "Data: 12/07/2026", 126);
}

/** @brief Abre o painel Sobre o equipamento ao tocar na opção correspondente. */
static void ui_flow_about_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_about_panel();
}

/** @brief Mantém preparada a ação futura de uma opção de manutenção. */
static void ui_flow_maintenance_option_cb(lv_event_t *event)
{
    (void)event;
}

/** @brief Cria uma opção clicável no painel de manutenção. */
static void ui_flow_maintenance_row(lv_obj_t *parent, const char *text, int32_t y)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, 340, 50);
    lv_obj_set_pos(row, 15, y);
    ui_flow_general_style_control(row);
    lv_obj_add_event_cb(row, ui_flow_maintenance_option_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *arrow = lv_label_create(row);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_color(arrow, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
}

/** @brief Libera o painel de manutenção antes da troca de conteúdo direito. */
static void ui_flow_destroy_maintenance_panel(void)
{
    if (s_ui_flow.maintenance_panel != NULL) {
        lv_obj_delete(s_ui_flow.maintenance_panel);
        s_ui_flow.maintenance_panel = NULL;
    }
}

/** @brief Exibe as opções disponíveis de manutenção e calibração. */
static void ui_flow_show_maintenance_panel(void)
{
    ui_flow_destroy_wifi_panel();
    ui_flow_destroy_bluetooth_panel();
    ui_flow_destroy_general_panel();
    ui_flow_destroy_about_panel();
    ui_flow_destroy_maintenance_panel();
    s_ui_flow.maintenance_panel = lv_obj_create(guider_ui.screen_configuracoes.screen);
    lv_obj_set_size(s_ui_flow.maintenance_panel, 370, 376);
    lv_obj_set_pos(s_ui_flow.maintenance_panel, 420, 21);
    lv_obj_set_style_bg_color(s_ui_flow.maintenance_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.maintenance_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.maintenance_panel, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui_flow.maintenance_panel, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui_flow.maintenance_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui_flow.maintenance_panel, LV_OBJ_FLAG_SCROLLABLE);
    ui_flow_maintenance_row(s_ui_flow.maintenance_panel, "Calibracao de Pressao", 14);
    ui_flow_maintenance_row(s_ui_flow.maintenance_panel, "Teste de Bomba", 70);
    ui_flow_maintenance_row(s_ui_flow.maintenance_panel, "Teste de Dreno", 126);
    ui_flow_maintenance_row(s_ui_flow.maintenance_panel, "Teste de Ultrassom", 182);
    ui_flow_maintenance_row(s_ui_flow.maintenance_panel, "Teste das Saidas (Bicos)", 238);
    ui_flow_maintenance_row(s_ui_flow.maintenance_panel, "Leitura de Sensores", 294);
}

/** @brief Abre o painel de manutenção ao tocar na opção correspondente. */
static void ui_flow_maintenance_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_maintenance_panel();
}

/** @brief Atualiza os labels de data e hora enquanto o menu principal está ativo. */
static void ui_flow_menu_clock_update_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!date_time_is_synchronized() || guider_ui.screen_menu_principal.label_menu_principal_hora == NULL ||
        guider_ui.screen_menu_principal.label_menu_principal_data == NULL) {
        return;
    }
    char time_text[6] = {0};
    char date_text[11] = {0};
    date_time_format_time(time_text, sizeof(time_text));
    date_time_format_date(date_text, sizeof(date_text));
    lv_label_set_text(guider_ui.screen_menu_principal.label_menu_principal_hora, time_text);
    lv_label_set_text(guider_ui.screen_menu_principal.label_menu_principal_data, date_text);
}

/** @brief Fecha o teclado local de credenciais Wi-Fi. */
static void ui_flow_close_wifi_keyboard(void)
{
    if (s_ui_flow.wifi_keyboard != NULL) {
        lv_obj_delete(s_ui_flow.wifi_keyboard);
        s_ui_flow.wifi_keyboard = NULL;
        s_ui_flow.wifi_password = NULL;
    }
}

/** @brief Fecha o popup local de erro Wi-Fi. */
static void ui_flow_close_wifi_popup(void)
{
    if (s_ui_flow.wifi_popup != NULL) {
        lv_obj_delete(s_ui_flow.wifi_popup);
        s_ui_flow.wifi_popup = NULL;
    }
}

/**
 * @brief Fecha o teclado ao cancelar a edição de senha.
 *
 * @param[in] event Evento LVGL de cancelamento.
 */
static void ui_flow_wifi_keyboard_cancel_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_wifi_keyboard();
}

/**
 * @brief Fecha o popup de erro ao tocar em seu botão.
 *
 * @param[in] event Evento LVGL do botão.
 */
static void ui_flow_wifi_popup_close_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_wifi_popup();
}

/**
 * @brief Exibe um popup local informando falha de autenticação/conexão.
 */
static void ui_flow_show_wifi_error(void)
{
    if (s_ui_flow.wifi_popup != NULL) {
        return;
    }
    s_ui_flow.wifi_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.wifi_popup, 360, 150);
    lv_obj_center(s_ui_flow.wifi_popup);
    lv_obj_set_style_bg_color(s_ui_flow.wifi_popup, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.wifi_popup, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.wifi_popup, lv_color_hex(0xc62828), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(s_ui_flow.wifi_popup);
    lv_label_set_text(title, "Falha ao conectar");
    lv_obj_set_style_text_color(title, lv_color_hex(0xff5252), LV_PART_MAIN);
    lv_obj_set_pos(title, 16, 16);
    lv_obj_t *message = lv_label_create(s_ui_flow.wifi_popup);
    lv_label_set_text(message, "Verifique a senha e tente novamente.");
    lv_obj_set_style_text_color(message, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_pos(message, 16, 52);
    lv_obj_t *close = lv_button_create(s_ui_flow.wifi_popup);
    lv_obj_set_size(close, 100, 36);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -12, 2);
    lv_obj_add_event_cb(close, ui_flow_wifi_popup_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(close);
    lv_label_set_text(label, "OK");
    lv_obj_center(label);
}

/**
 * @brief Envia as credenciais digitadas ao gerenciador Wi-Fi do core 0.
 *
 * @param[in] event Evento de confirmação do teclado.
 */
static void ui_flow_wifi_connect_cb(lv_event_t *event)
{
    (void)event;
    if (s_ui_flow.wifi_password != NULL) {
        wifi_manager_connect(s_ui_flow.wifi_selected_ssid, lv_textarea_get_text(s_ui_flow.wifi_password));
    }
    ui_flow_close_wifi_keyboard();
}

/**
 * @brief Cria teclado de senha na metade inferior da tela.
 *
 * @param[in] ssid SSID selecionado na lista de redes.
 */
static void ui_flow_show_wifi_keyboard(const char *ssid)
{
    ui_flow_close_wifi_keyboard();
    strncpy(s_ui_flow.wifi_selected_ssid, ssid, DRIVER_WIFI_SSID_MAX_LENGTH);
    s_ui_flow.wifi_selected_ssid[DRIVER_WIFI_SSID_MAX_LENGTH] = '\0';
    s_ui_flow.wifi_keyboard = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.wifi_keyboard, 800, 350);
    lv_obj_set_pos(s_ui_flow.wifi_keyboard, 0, 130);
    lv_obj_set_style_bg_color(s_ui_flow.wifi_keyboard, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.wifi_keyboard, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.wifi_keyboard, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(s_ui_flow.wifi_keyboard);
    lv_label_set_text_fmt(title, "Senha: %s", s_ui_flow.wifi_selected_ssid);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_pos(title, 16, 5);
    s_ui_flow.wifi_password = lv_textarea_create(s_ui_flow.wifi_keyboard);
    lv_obj_set_size(s_ui_flow.wifi_password, 500, 42);
    lv_obj_set_pos(s_ui_flow.wifi_password, 16, 27);
    lv_textarea_set_password_mode(s_ui_flow.wifi_password, true);
    lv_textarea_set_one_line(s_ui_flow.wifi_password, true);
    lv_obj_t *keyboard = lv_keyboard_create(s_ui_flow.wifi_keyboard);
    lv_obj_set_size(keyboard, 760, 220);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(keyboard, s_ui_flow.wifi_password);
    lv_obj_add_event_cb(keyboard, ui_flow_wifi_connect_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, ui_flow_wifi_keyboard_cancel_cb, LV_EVENT_CANCEL, NULL);
}

/**
 * @brief Verifica se o estado Wi-Fi mudou desde a última atualização visual.
 *
 * @param[in] status Estado mais recente do gerenciador Wi-Fi.
 * @return @c true se a interface precisa ser redesenhada.
 */
static bool ui_flow_wifi_status_changed(const wifi_manager_status_t *status)
{
    if (!s_ui_flow.wifi_status_valid || status->enabled != s_ui_flow.wifi_last_status.enabled ||
        status->scanning != s_ui_flow.wifi_last_status.scanning ||
        status->connected != s_ui_flow.wifi_last_status.connected ||
        status->connecting != s_ui_flow.wifi_last_status.connecting ||
        status->connection_failed != s_ui_flow.wifi_last_status.connection_failed ||
        strcmp(status->connected_ssid, s_ui_flow.wifi_last_status.connected_ssid) != 0 ||
        status->network_count != s_ui_flow.wifi_last_status.network_count) {
        return true;
    }
    for (uint16_t index = 0; index < status->network_count; index++) {
        const driver_wifi_network_t *current = &status->networks[index];
        const driver_wifi_network_t *previous = &s_ui_flow.wifi_last_status.networks[index];
        if (current->rssi != previous->rssi || current->secured != previous->secured || strcmp(current->ssid, previous->ssid) != 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Abre o teclado ao selecionar uma rede da lista.
 *
 * @param[in] event Evento LVGL do item de rede.
 */
static void ui_flow_wifi_network_cb(lv_event_t *event)
{
    ui_flow_show_wifi_keyboard(lv_event_get_user_data(event));
}

/**
 * @brief Atualiza o estado e a lista rolável de redes no painel Wi-Fi.
 *
 * @param[in] timer Timer LVGL associado ao painel.
 */
static void ui_flow_wifi_update_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_ui_flow.wifi_panel == NULL || s_ui_flow.wifi_status == NULL || s_ui_flow.wifi_network_list == NULL) {
        return;
    }
    wifi_manager_status_t status = {0};
    if (wifi_manager_get_status(&status) != ESP_OK) {
        return;
    }
    if (!ui_flow_wifi_status_changed(&status)) {
        return;
    }
    s_ui_flow.wifi_last_status = status;
    s_ui_flow.wifi_status_valid = true;
    if (status.connection_failed) {
        ui_flow_close_wifi_keyboard();
        ui_flow_show_wifi_error();
    }
    if (s_ui_flow.wifi_toggle != NULL) {
        if (status.enabled) {
            lv_obj_add_state(s_ui_flow.wifi_toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_ui_flow.wifi_toggle, LV_STATE_CHECKED);
        }
    }
    if (!status.enabled) {
        lv_label_set_text(s_ui_flow.wifi_connection_label, "");
        lv_obj_set_x(s_ui_flow.wifi_status, 16);
        lv_label_set_text(s_ui_flow.wifi_status, "Wi-Fi desligado");
    } else if (status.scanning) {
        lv_label_set_text(s_ui_flow.wifi_connection_label, "");
        lv_obj_set_x(s_ui_flow.wifi_status, 16);
        lv_label_set_text(s_ui_flow.wifi_status, "Procurando redes...");
    } else if (status.connecting) {
        lv_label_set_text(s_ui_flow.wifi_connection_label, "");
        lv_obj_set_x(s_ui_flow.wifi_status, 16);
        lv_label_set_text_fmt(s_ui_flow.wifi_status, "Conectando: %s", status.connected_ssid);
    } else if (status.connected) {
        lv_label_set_text(s_ui_flow.wifi_connection_label, "Conectado");
        lv_obj_set_x(s_ui_flow.wifi_status, 116);
        lv_label_set_text(s_ui_flow.wifi_status, status.connected_ssid);
    } else {
        lv_label_set_text(s_ui_flow.wifi_connection_label, "");
        lv_obj_set_x(s_ui_flow.wifi_status, 16);
        lv_label_set_text(s_ui_flow.wifi_status, "Nenhuma rede conectada");
    }
    lv_obj_clean(s_ui_flow.wifi_network_list);
    uint16_t displayed_networks = 0;
    for (uint16_t index = 0; index < status.network_count; index++) {
        if (status.connected && strcmp(status.networks[index].ssid, status.connected_ssid) == 0) {
            continue;
        }
        displayed_networks++;
    }
    if (status.enabled && !status.scanning && displayed_networks == 0) {
        lv_obj_t *empty = lv_label_create(s_ui_flow.wifi_network_list);
        lv_label_set_text(empty, "Nenhuma rede encontrada");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xffffff), LV_PART_MAIN);
    }
    for (uint16_t index = 0; index < status.network_count; index++) {
        if (status.connected && strcmp(status.networks[index].ssid, status.connected_ssid) == 0) {
            continue;
        }
        lv_obj_t *network = lv_button_create(s_ui_flow.wifi_network_list);
        lv_obj_add_event_cb(network, ui_flow_wifi_network_cb, LV_EVENT_CLICKED, s_ui_flow.wifi_last_status.networks[index].ssid);
        lv_obj_set_size(network, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(network, lv_color_hex(0x202020), LV_PART_MAIN);
        lv_obj_set_style_border_width(network, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(network, lv_color_hex(0x2f3539), LV_PART_MAIN);
        lv_obj_t *network_label = lv_label_create(network);
        lv_label_set_text_fmt(network_label, "%s  %ddBm%s", status.networks[index].ssid, status.networks[index].rssi,
                              status.networks[index].secured ? "  *" : "");
        lv_obj_set_width(network_label, LV_PCT(92));
        lv_obj_set_style_text_color(network_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_center(network_label);
    }
}

/** @brief Verifica se o estado Bluetooth mudou desde a última atualização visual. */
static bool ui_flow_bluetooth_status_changed(const bluetooth_manager_status_t *status)
{
    if (!s_ui_flow.bluetooth_status_valid || status->enabled != s_ui_flow.bluetooth_last_status.enabled ||
        status->scanning != s_ui_flow.bluetooth_last_status.scanning ||
        status->device_count != s_ui_flow.bluetooth_last_status.device_count) {
        return true;
    }
    for (uint16_t index = 0; index < status->device_count; index++) {
        const driver_bluetooth_device_t *current = &status->devices[index];
        const driver_bluetooth_device_t *previous = &s_ui_flow.bluetooth_last_status.devices[index];
        if (current->rssi != previous->rssi || strcmp(current->name, previous->name) != 0 ||
            strcmp(current->address, previous->address) != 0) {
            return true;
        }
    }
    return false;
}

/** @brief Atualiza o estado e a lista rolável de dispositivos Bluetooth encontrados. */
static void ui_flow_bluetooth_update_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_ui_flow.bluetooth_panel == NULL || s_ui_flow.bluetooth_status == NULL ||
        s_ui_flow.bluetooth_device_list == NULL) {
        return;
    }
    bluetooth_manager_status_t status = {0};
    if (bluetooth_manager_get_status(&status) != ESP_OK || !ui_flow_bluetooth_status_changed(&status)) {
        return;
    }
    s_ui_flow.bluetooth_last_status = status;
    s_ui_flow.bluetooth_status_valid = true;
    if (s_ui_flow.bluetooth_toggle != NULL) {
        if (status.enabled) {
            lv_obj_add_state(s_ui_flow.bluetooth_toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_ui_flow.bluetooth_toggle, LV_STATE_CHECKED);
        }
    }
    lv_label_set_text(s_ui_flow.bluetooth_status, !status.enabled ? "Bluetooth desligado" :
                      status.scanning ? "Procurando dispositivos..." : "Dispositivos encontrados");
    lv_obj_clean(s_ui_flow.bluetooth_device_list);
    if (status.enabled && status.device_count == 0) {
        lv_obj_t *empty = lv_label_create(s_ui_flow.bluetooth_device_list);
        lv_label_set_text(empty, status.scanning ? "Nenhum dispositivo ainda" : "Nenhum dispositivo encontrado");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xffffff), LV_PART_MAIN);
    }
    for (uint16_t index = 0; index < status.device_count; index++) {
        lv_obj_t *device = lv_button_create(s_ui_flow.bluetooth_device_list);
        lv_obj_set_size(device, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(device, lv_color_hex(0x202020), LV_PART_MAIN);
        lv_obj_set_style_border_width(device, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(device, lv_color_hex(0x2f3539), LV_PART_MAIN);
        lv_obj_t *label = lv_label_create(device);
        lv_label_set_text_fmt(label, "%s  %ddBm", status.devices[index].name, status.devices[index].rssi);
        lv_obj_set_width(label, LV_PCT(92));
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_center(label);
    }
}

/** @brief Encaminha a mudança do toggle Bluetooth ao gerenciador no core 0. */
static void ui_flow_bluetooth_toggle_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target_obj(event);
    const bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    if (!enabled) {
        lv_label_set_text(s_ui_flow.bluetooth_status, "Bluetooth desligado");
        lv_obj_clean(s_ui_flow.bluetooth_device_list);
        s_ui_flow.bluetooth_last_status = (bluetooth_manager_status_t){0};
        s_ui_flow.bluetooth_status_valid = true;
    }
    bluetooth_manager_set_enabled(enabled);
}

/** @brief Cria o painel local e rolável de informações Bluetooth. */
static void ui_flow_show_bluetooth_panel(void)
{
    ui_flow_destroy_wifi_panel();
    ui_flow_destroy_general_panel();
    ui_flow_destroy_about_panel();
    ui_flow_destroy_maintenance_panel();
    if (bluetooth_manager_start() != ESP_OK) {
        return;
    }
    lv_obj_t *screen = guider_ui.screen_configuracoes.screen;
    s_ui_flow.bluetooth_panel = lv_obj_create(screen);
    lv_obj_set_size(s_ui_flow.bluetooth_panel, 370, 376);
    lv_obj_set_pos(s_ui_flow.bluetooth_panel, 420, 21);
    lv_obj_set_style_bg_color(s_ui_flow.bluetooth_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui_flow.bluetooth_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.bluetooth_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.bluetooth_panel, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui_flow.bluetooth_panel, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui_flow.bluetooth_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui_flow.bluetooth_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = lv_button_create(s_ui_flow.bluetooth_panel);
    lv_obj_set_size(header, 340, 50);
    lv_obj_set_pos(header, 15, 13);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Bluetooth");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 14, 0);
    s_ui_flow.bluetooth_toggle = lv_switch_create(header);
    lv_obj_align(s_ui_flow.bluetooth_toggle, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_bg_color(s_ui_flow.bluetooth_toggle, lv_color_hex(0x263238), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_ui_flow.bluetooth_toggle, lv_color_hex(0x00c853), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_ui_flow.bluetooth_toggle, lv_color_hex(0x455a64), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(s_ui_flow.bluetooth_toggle, ui_flow_bluetooth_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_ui_flow.bluetooth_status = lv_label_create(s_ui_flow.bluetooth_panel);
    lv_obj_set_pos(s_ui_flow.bluetooth_status, 16, 72);
    lv_obj_set_style_text_color(s_ui_flow.bluetooth_status, lv_color_hex(0xffffff), LV_PART_MAIN);
    s_ui_flow.bluetooth_device_list = lv_obj_create(s_ui_flow.bluetooth_panel);
    lv_obj_set_size(s_ui_flow.bluetooth_device_list, 340, 270);
    lv_obj_align(s_ui_flow.bluetooth_device_list, LV_ALIGN_TOP_MID, 0, 94);
    lv_obj_set_style_bg_color(s_ui_flow.bluetooth_device_list, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.bluetooth_device_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui_flow.bluetooth_device_list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui_flow.bluetooth_device_list, lv_color_hex(0x455a64), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_ui_flow.bluetooth_device_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    s_ui_flow.bluetooth_timer = lv_timer_create(ui_flow_bluetooth_update_cb, 500, NULL);
    s_ui_flow.bluetooth_status_valid = false;
    ui_flow_bluetooth_update_cb(s_ui_flow.bluetooth_timer);
}

/** @brief Libera o painel Bluetooth e seu timer antes de trocar de conteúdo. */
static void ui_flow_destroy_bluetooth_panel(void)
{
    if (s_ui_flow.bluetooth_timer != NULL) {
        lv_timer_delete(s_ui_flow.bluetooth_timer);
        s_ui_flow.bluetooth_timer = NULL;
    }
    if (s_ui_flow.bluetooth_panel != NULL) {
        lv_obj_delete(s_ui_flow.bluetooth_panel);
    }
    s_ui_flow.bluetooth_panel = NULL;
    s_ui_flow.bluetooth_toggle = NULL;
    s_ui_flow.bluetooth_status = NULL;
    s_ui_flow.bluetooth_device_list = NULL;
    s_ui_flow.bluetooth_status_valid = false;
}

/**
 * @brief Encaminha a mudança do toggle ao gerenciador Wi-Fi no core 0.
 *
 * @param[in] event Evento de valor alterado do switch LVGL.
 */
static void ui_flow_wifi_toggle_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target_obj(event);
    wifi_manager_set_enabled(lv_obj_has_state(toggle, LV_STATE_CHECKED));
}

/**
 * @brief Cria o painel local e rolável de informações de Wi-Fi.
 */
static void ui_flow_show_wifi_panel(void)
{
    ui_flow_destroy_bluetooth_panel();
    ui_flow_destroy_wifi_panel();
    ui_flow_destroy_general_panel();
    ui_flow_destroy_about_panel();
    ui_flow_destroy_maintenance_panel();
    lv_obj_t *screen = guider_ui.screen_configuracoes.screen;
    s_ui_flow.wifi_panel = lv_obj_create(screen);
    lv_obj_set_size(s_ui_flow.wifi_panel, 370, 376);
    lv_obj_set_pos(s_ui_flow.wifi_panel, 420, 21);
    lv_obj_set_style_bg_color(s_ui_flow.wifi_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui_flow.wifi_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.wifi_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.wifi_panel, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui_flow.wifi_panel, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui_flow.wifi_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui_flow.wifi_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_button_create(s_ui_flow.wifi_panel);
    lv_obj_set_size(header, 340, 50);
    lv_obj_set_pos(header, 15, 13);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 16, 0);
    s_ui_flow.wifi_toggle = lv_switch_create(header);
    lv_obj_align(s_ui_flow.wifi_toggle, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_style_bg_color(s_ui_flow.wifi_toggle, lv_color_hex(0x263238), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_ui_flow.wifi_toggle, lv_color_hex(0x455a64), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_ui_flow.wifi_toggle, lv_color_hex(0x00c853), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_ui_flow.wifi_toggle, ui_flow_wifi_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_ui_flow.wifi_connection_label = lv_label_create(s_ui_flow.wifi_panel);
    lv_obj_set_pos(s_ui_flow.wifi_connection_label, 16, 70);
    lv_obj_set_style_text_color(s_ui_flow.wifi_connection_label, lv_color_hex(0x00c853), LV_PART_MAIN);
    s_ui_flow.wifi_status = lv_label_create(s_ui_flow.wifi_panel);
    lv_obj_set_pos(s_ui_flow.wifi_status, 116, 70);
    lv_obj_set_style_text_color(s_ui_flow.wifi_status, lv_color_hex(0xffffff), LV_PART_MAIN);
    s_ui_flow.wifi_network_list = lv_obj_create(s_ui_flow.wifi_panel);
    lv_obj_set_size(s_ui_flow.wifi_network_list, 340, 270);
    lv_obj_align(s_ui_flow.wifi_network_list, LV_ALIGN_TOP_MID, 0, 94);
    lv_obj_set_flex_flow(s_ui_flow.wifi_network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_ui_flow.wifi_network_list, LV_DIR_VER);
    lv_obj_set_style_bg_opa(s_ui_flow.wifi_network_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.wifi_network_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_ui_flow.wifi_network_list, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui_flow.wifi_network_list, lv_color_hex(0x455a64), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_ui_flow.wifi_network_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    s_ui_flow.wifi_timer = lv_timer_create(ui_flow_wifi_update_cb, 500, NULL);
    s_ui_flow.wifi_status_valid = false;
    ui_flow_wifi_update_cb(s_ui_flow.wifi_timer);
}

/**
 * @brief Libera o painel Wi-Fi e seu timer antes de trocar de tela.
 */
static void ui_flow_destroy_wifi_panel(void)
{
    ui_flow_close_wifi_keyboard();
    ui_flow_close_wifi_popup();
    if (s_ui_flow.wifi_timer != NULL) {
        lv_timer_delete(s_ui_flow.wifi_timer);
        s_ui_flow.wifi_timer = NULL;
    }
    if (s_ui_flow.wifi_panel != NULL) {
        lv_obj_delete(s_ui_flow.wifi_panel);
    }
    s_ui_flow.wifi_panel = NULL;
    s_ui_flow.wifi_toggle = NULL;
    s_ui_flow.wifi_status = NULL;
    s_ui_flow.wifi_connection_label = NULL;
    s_ui_flow.wifi_network_list = NULL;
    s_ui_flow.wifi_status_valid = false;
}

/**
 * @brief Abre o painel Wi-Fi ao tocar na opção correspondente.
 *
 * @param[in] event Evento LVGL do botão Wi-Fi.
 */
static void ui_flow_wifi_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_wifi_panel();
}

/**
 * @brief Abre o painel Bluetooth ao tocar na opção correspondente.
 *
 * @param[in] event Evento LVGL do botão Bluetooth.
 */
static void ui_flow_bluetooth_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_destroy_bluetooth_panel();
    ui_flow_show_bluetooth_panel();
}

/**
 * @brief Abre a tela de configurações ao tocar no botão do menu principal.
 *
 * @param[in] event Evento LVGL do botão Configurações.
 */
static void ui_flow_settings_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_settings();
}

/**
 * @brief Retorna ao menu principal ao tocar em Voltar ou Home.
 *
 * @param[in] event Evento LVGL de um dos botões de retorno.
 */
static void ui_flow_main_menu_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_main_menu();
}

/**
 * @brief Cria sob demanda e carrega a tela do osciloscópio.
 *
 * @param[in] event Evento LVGL da escolha do tipo de bico.
 */
static void ui_flow_oscilloscope_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_destroy_wifi_panel();
    ui_flow_destroy_bluetooth_panel();
    if (s_ui_flow.menu_clock_timer != NULL) {
        lv_timer_delete(s_ui_flow.menu_clock_timer);
        s_ui_flow.menu_clock_timer = NULL;
    }
    osc_set_menu_callback(ui_flow_oscilloscope_menu_cb);
    if (osc_create(NULL) != ESP_OK || osc_get_screen() == NULL) {
        return;
    }
    lv_screen_load_anim(osc_get_screen(), LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * @brief Cria e exibe a primeira etapa da seleção de bicos.
 *
 * @param[in] event Evento LVGL dos botões Modo Teste ou Limpeza de Bicos.
 */
static void ui_flow_bicos_step_one_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_bicos_step_one();
}

/**
 * @brief Avança da escolha do bico para a escolha do modo de teste.
 *
 * @param[in] event Evento LVGL do tipo de bico selecionado.
 */
static void ui_flow_bicos_step_two_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_bicos_step_two();
}

/** @brief Salva o tipo de bico escolhido e avança para a seleção do modo de teste. */
static void ui_flow_injector_type_button_cb(lv_event_t *event)
{
    s_ui_flow.injector_type = (ui_flow_injector_type_t)(uintptr_t)lv_event_get_user_data(event);
    ui_flow_show_bicos_step_two();
}

/** @brief Abre a configuração manual de parâmetros do ciclo. */
static void ui_flow_bicos_manual_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_bicos_config_manual();
}

/** @brief Abre a confirmação final antes de iniciar o ciclo. */
static void ui_flow_ready_to_start_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_ready_to_start();
}

/** @brief Retorna do osciloscópio ao menu sem destruir sua tela persistente. */
static void ui_flow_oscilloscope_menu_cb(void)
{
    s_ui_flow.preserve_oscilloscope_screen = true;
    ui_flow_show_main_menu();
    osc_destroy();
}

/**
 * @brief Cria, conecta e carrega o menu principal.
 *
 * A tela anterior é removida após a troca para não manter objetos LVGL
 * ocultos alocados na memória.
 */
static void ui_flow_show_main_menu(void)
{
    ui_flow_close_manual_popup();
    ui_flow_destroy_general_panel();
    ui_flow_destroy_about_panel();
    ui_flow_destroy_maintenance_panel();
    const bool delete_previous_screen = !s_ui_flow.preserve_oscilloscope_screen;
    s_ui_flow.preserve_oscilloscope_screen = false;
    ui_flow_destroy_wifi_panel();
    ui_flow_destroy_bluetooth_panel();
    if (s_ui_flow.menu_clock_timer != NULL) {
        lv_timer_delete(s_ui_flow.menu_clock_timer);
        s_ui_flow.menu_clock_timer = NULL;
    }
    memset(&guider_ui.screen_menu_principal, 0, sizeof(guider_ui.screen_menu_principal));
    setup_screen_menu_principal(&guider_ui);
    if (guider_ui.screen_menu_principal.screen == NULL) {
        return;
    }
    if (guider_ui.screen_menu_principal.button_configuracoes != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_menu_principal.button_configuracoes,
                            ui_flow_settings_button_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }
    if (guider_ui.screen_menu_principal.button_modo_teste != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_menu_principal.button_modo_teste,
                            ui_flow_bicos_step_one_button_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }
    if (guider_ui.screen_menu_principal.button_limpeza_bico != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_menu_principal.button_limpeza_bico,
                            ui_flow_bicos_step_one_button_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }
    s_ui_flow.menu_clock_timer = lv_timer_create(ui_flow_menu_clock_update_cb, 1000, NULL);
    ui_flow_menu_clock_update_cb(s_ui_flow.menu_clock_timer);
    lv_screen_load_anim(guider_ui.screen_menu_principal.screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, delete_previous_screen);
}

/** @brief Cria, conecta e carrega a tela de escolha do tipo de bico. */
static void ui_flow_show_bicos_step_one(void)
{
    ui_flow_close_manual_popup();
    if (s_ui_flow.menu_clock_timer != NULL) {
        lv_timer_delete(s_ui_flow.menu_clock_timer);
        s_ui_flow.menu_clock_timer = NULL;
    }
    memset(&guider_ui.screen_bicos_step_one, 0, sizeof(guider_ui.screen_bicos_step_one));
    setup_screen_bicos_step_one(&guider_ui);
    if (guider_ui.screen_bicos_step_one.screen == NULL) {
        return;
    }
    if (guider_ui.screen_bicos_step_one.button_voltar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_one.button_voltar,
                            ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_bicos_step_one.button_home != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_one.button_home,
                            ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_bicos_step_one.button_bico_12v_comum != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_one.button_bico_12v_comum,
                            ui_flow_injector_type_button_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)UI_FLOW_INJECTOR_12V);
    }
    if (guider_ui.screen_bicos_step_one.button_bico_gdi != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_one.button_bico_gdi,
                            ui_flow_injector_type_button_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)UI_FLOW_INJECTOR_75V_GDI);
    }
    lv_screen_load_anim(guider_ui.screen_bicos_step_one.screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/** @brief Cria, conecta e carrega a tela de escolha do modo de teste. */
static void ui_flow_show_bicos_step_two(void)
{
    ui_flow_close_manual_popup();
    memset(&guider_ui.screen_bicos_step_two, 0, sizeof(guider_ui.screen_bicos_step_two));
    setup_screen_bicos_step_two(&guider_ui);
    if (guider_ui.screen_bicos_step_two.screen == NULL) {
        return;
    }
    if (guider_ui.screen_bicos_step_two.button_voltar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_two.button_voltar,
                            ui_flow_bicos_step_one_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_bicos_step_two.button_home != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_two.button_home,
                            ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_bicos_step_two.button_modo_automatico != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_two.button_modo_automatico,
                            ui_flow_oscilloscope_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_bicos_step_two.button_modo_manual != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_two.button_modo_manual,
                            ui_flow_bicos_manual_button_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_screen_load_anim(guider_ui.screen_bicos_step_two.screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/** @brief Cria, conecta e carrega a tela de configuração manual do ciclo. */
static void ui_flow_show_bicos_config_manual(void)
{
    ui_flow_close_manual_popup();
    memset(&guider_ui.screen_bicos_step_config_manual, 0, sizeof(guider_ui.screen_bicos_step_config_manual));
    setup_screen_bicos_step_config_manual(&guider_ui);
    if (guider_ui.screen_bicos_step_config_manual.screen == NULL) {
        return;
    }
    ui_flow_manual_create_navigation_indicators();
    ui_flow_manual_update_labels();
    if (guider_ui.screen_bicos_step_config_manual.button_voltar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_config_manual.button_voltar,
                            ui_flow_bicos_step_two_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_bicos_step_config_manual.button_avancar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_bicos_step_config_manual.button_avancar,
                            ui_flow_ready_to_start_button_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *buttons[] = {
        guider_ui.screen_bicos_step_config_manual.button_pressao,
        guider_ui.screen_bicos_step_config_manual.button_pulso_ms,
        guider_ui.screen_bicos_step_config_manual.button_rpm,
        guider_ui.screen_bicos_step_config_manual.button_ciclos,
        guider_ui.screen_bicos_step_config_manual.button_tempo_de_pausa,
        guider_ui.screen_bicos_step_config_manual.button_temperatura
    };
    for (uint32_t setting = UI_FLOW_MANUAL_PRESSURE; setting <= UI_FLOW_MANUAL_TEMPERATURE; setting++) {
        if (buttons[setting] != NULL) {
            lv_obj_add_event_cb(buttons[setting], ui_flow_manual_setting_button_cb,
                                LV_EVENT_CLICKED, (void *)(uintptr_t)setting);
        }
    }
    lv_screen_load_anim(guider_ui.screen_bicos_step_config_manual.screen,
                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/** @brief Cria, preenche e carrega a tela de confirmação do ciclo. */
static void ui_flow_show_ready_to_start(void)
{
    ui_flow_close_manual_popup();
    memset(&guider_ui.screen_pronto_pra_iniciar, 0, sizeof(guider_ui.screen_pronto_pra_iniciar));
    setup_screen_pronto_pra_iniciar(&guider_ui);
    if (guider_ui.screen_pronto_pra_iniciar.screen == NULL) {
        return;
    }
    ui_flow_ready_populate_info();
    if (guider_ui.screen_pronto_pra_iniciar.button_voltar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_pronto_pra_iniciar.button_voltar,
                            ui_flow_bicos_manual_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_pronto_pra_iniciar.button_iniciar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_pronto_pra_iniciar.button_iniciar,
                            ui_flow_oscilloscope_button_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_screen_load_anim(guider_ui.screen_pronto_pra_iniciar.screen,
                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * @brief Cria, conecta e carrega a tela de configurações.
 *
 * Os botões Voltar e Home retornam ambos ao menu principal nesta etapa.
 */
static void ui_flow_show_settings(void)
{
    if (s_ui_flow.menu_clock_timer != NULL) {
        lv_timer_delete(s_ui_flow.menu_clock_timer);
        s_ui_flow.menu_clock_timer = NULL;
    }
    memset(&guider_ui.screen_configuracoes, 0, sizeof(guider_ui.screen_configuracoes));
    setup_screen_configuracoes(&guider_ui);
    if (guider_ui.screen_configuracoes.screen == NULL) {
        return;
    }
    if (guider_ui.screen_configuracoes.button_configuracoes_voltar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.button_configuracoes_voltar,
                            ui_flow_main_menu_button_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }
    if (guider_ui.screen_configuracoes.button_home != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.button_home,
                            ui_flow_main_menu_button_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }
    if (guider_ui.screen_configuracoes.container_configuracoes_button_wifi != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.container_configuracoes_button_wifi,
                            ui_flow_wifi_button_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }
    if (guider_ui.screen_configuracoes.container_configuracoes_button_bluetooth != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.container_configuracoes_button_bluetooth,
                            ui_flow_bluetooth_button_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }
    if (guider_ui.screen_configuracoes.button_parametros_gerais != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.button_parametros_gerais,
                            ui_flow_general_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_configuracoes.button_sobre != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.button_sobre,
                            ui_flow_about_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_configuracoes.button_manutencao != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.button_manutencao,
                            ui_flow_maintenance_button_cb, LV_EVENT_CLICKED, NULL);
    }
    ui_flow_show_wifi_panel();
    lv_screen_load_anim(guider_ui.screen_configuracoes.screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * @brief Atualiza o valor da barra da splash durante a animação.
 *
 * @param[in] object Barra LVGL convertida para ponteiro genérico.
 * @param[in] value Novo valor entre 0 e 100.
 */
static void ui_flow_set_splash_progress(void *object, int32_t value)
{
    lv_bar_set_value(object, value, LV_ANIM_OFF);
}

/**
 * @brief Cria a tela principal e substitui imediatamente a splash.
 *
 * @param[in] animation Animação da barra recém-concluída.
 */
static void ui_flow_splash_complete_cb(lv_anim_t *animation)
{
    (void)animation;
    ui_flow_show_main_menu();
}

/**
 * @brief Aplica localmente o estilo da barra sem alterar o código gerado.
 *
 * @param[in] bar Barra exportada pela tela de splash do GUI Guider.
 */
static void ui_flow_configure_splash_bar(lv_obj_t *bar)
{
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, UI_FLOW_SPLASH_BAR_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, UI_FLOW_SPLASH_BAR_RADIUS, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xeeeeee), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xc62828), LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

/**
 * @brief Remove localmente o arredondamento do container criado pelo GUI Guider.
 *
 * @param[in] container Container principal da tela de splash.
 */
static void ui_flow_configure_splash_container(lv_obj_t *container)
{
    lv_obj_set_style_radius(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief Inicia a animação de carregamento da splash.
 */
static void ui_flow_start_splash_animation(void)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_ui_flow.splash_bar);
    lv_anim_set_values(&animation, 0, 100);
    lv_anim_set_duration(&animation, UI_FLOW_SPLASH_DURATION_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_set_exec_cb(&animation, ui_flow_set_splash_progress);
    lv_anim_set_ready_cb(&animation, ui_flow_splash_complete_cb);
    lv_anim_start(&animation);
}

/**
 * @brief Inicia a splash gerada pelo GUI Guider e o fluxo inicial da interface.
 *
 * @return @c ESP_OK em caso de sucesso ou @c ESP_ERR_INVALID_STATE se faltar a barra.
 */
esp_err_t ui_flow_start(void)
{
    memset(&guider_ui, 0, sizeof(guider_ui));
    setup_ui(&guider_ui);
    ESP_RETURN_ON_FALSE(guider_ui.screen_splash.container_screen_splash != NULL,
                        ESP_ERR_INVALID_STATE,
                        "ui_flow",
                        "container da splash ausente");
    ui_flow_configure_splash_container(guider_ui.screen_splash.container_screen_splash);
    s_ui_flow.splash_bar = guider_ui.screen_splash.container_screen_splash_bar_screen_splash;
    ESP_RETURN_ON_FALSE(s_ui_flow.splash_bar != NULL, ESP_ERR_INVALID_STATE, "ui_flow", "barra da splash ausente");
    ui_flow_configure_splash_bar(s_ui_flow.splash_bar);
    ui_flow_start_splash_animation();
    return ESP_OK;
}
