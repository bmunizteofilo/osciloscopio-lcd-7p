#include "ui_flow.h"

#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "esp_check.h"
#include "nvs.h"
#include "gui_guider.h"
#include "gg_utils.h"
#include "wifi_manager.h"
#include "bluetooth_manager.h"
#include "date_time.h"
#include "osc.h"
#include "general_settings.h"
#include "report_storage.h"
#include "acquisition_stream.h"

#define UI_FLOW_SPLASH_DURATION_MS 5000U
#define UI_FLOW_SPLASH_BAR_RADIUS 15
#define UI_FLOW_STANDBY_TIMEOUT_MS 90000U
#define UI_FLOW_MENU_DRAG_THRESHOLD_PX 18
#define UI_FLOW_MENU_LAYOUT_NAMESPACE "menu_layout"
#define UI_FLOW_MENU_LAYOUT_KEY "ordem"

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

/** @brief Receita identificada de um teste automático futuro. */
typedef struct {
    const char *description;
    uint8_t operation_mode;
} ui_flow_automatic_test_t;

/** @brief Identificadores estáveis dos cards que podem ocupar os slots do menu. */
typedef enum {
    UI_FLOW_MENU_CLEANING,
    UI_FLOW_MENU_TEST_MODE,
    UI_FLOW_MENU_REPORTS,
    UI_FLOW_MENU_DIAGNOSTIC,
    UI_FLOW_MENU_SETTINGS,
    UI_FLOW_MENU_ITEM_COUNT
} ui_flow_menu_item_t;

/** @brief Geometria fixa de uma posição do menu principal. */
typedef struct {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} ui_flow_menu_slot_t;

/** @brief Campos editáveis dos dados apresentados no fechamento do ciclo. */
typedef enum {
    UI_FLOW_CLIENT_NAME,
    UI_FLOW_CLIENT_DATE,
    UI_FLOW_CLIENT_PHONE,
    UI_FLOW_CLIENT_VEHICLE,
    UI_FLOW_CLIENT_PLATE,
    UI_FLOW_CLIENT_KM,
    UI_FLOW_CLIENT_OBSERVATIONS,
    UI_FLOW_CLIENT_FIELD_COUNT
} ui_flow_client_field_t;

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
    lv_timer_t *cycle_finished_timer;
    lv_obj_t *client_keyboard;
    lv_obj_t *client_textarea;
    lv_obj_t *client_date_popup;
    lv_obj_t *client_date_calendar;
    lv_obj_t *report_popup;
    lv_obj_t *report_search_keyboard;
    lv_obj_t *report_search_textarea;
    lv_obj_t *report_search_label;
    lv_obj_t *client_value_labels[UI_FLOW_CLIENT_FIELD_COUNT];
    ui_flow_client_field_t client_active_field;
    lv_calendar_date_t client_selected_date;
    lv_calendar_date_t client_highlighted_date;
    char client_values[UI_FLOW_CLIENT_FIELD_COUNT][96];
    char selected_test_description[REPORT_STORAGE_TEXT_LENGTH];
    char report_search_text[REPORT_STORAGE_TEXT_LENGTH];
    char test_start_time[6];
    char test_end_time[6];
    report_storage_record_t selected_report;
    bool selected_report_valid;
    bool final_report_from_list;
    lv_obj_t *menu_drag_source;
    lv_obj_t *menu_drag_target;
    int32_t menu_drag_start_x;
    int32_t menu_drag_start_y;
    int32_t menu_drag_source_x;
    int32_t menu_drag_source_y;
    bool menu_dragging;
    uint8_t menu_slot_order[UI_FLOW_MENU_ITEM_COUNT];
    bool menu_layout_loaded;
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
    uint8_t automatic_operation_mode;
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

/** @brief Ordem visual padrão dos cards nos cinco slots do menu. */
static const uint8_t s_ui_flow_default_menu_order[UI_FLOW_MENU_ITEM_COUNT] = {
    UI_FLOW_MENU_CLEANING,
    UI_FLOW_MENU_TEST_MODE,
    UI_FLOW_MENU_REPORTS,
    UI_FLOW_MENU_DIAGNOSTIC,
    UI_FLOW_MENU_SETTINGS,
};

/** @brief Posições grandes superiores e posições compactas inferiores do menu principal. */
static const ui_flow_menu_slot_t s_ui_flow_menu_slots[UI_FLOW_MENU_ITEM_COUNT] = {
    {.x = 10, .y = 92, .width = 385, .height = 181},
    {.x = 405, .y = 92, .width = 385, .height = 181},
    {.x = 10, .y = 288, .width = 253, .height = 166},
    {.x = 273, .y = 288, .width = 253, .height = 166},
    {.x = 537, .y = 288, .width = 253, .height = 166},
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
static void ui_flow_show_automatic_tests(void);
static void ui_flow_show_automatic_tests_second_page(void);
static void ui_flow_show_standby(void);
static void ui_flow_show_cycle_finished(void);
static void ui_flow_show_client_data(void);
static void ui_flow_show_client_date_calendar(void);
static void ui_flow_show_final_report(void);
static void ui_flow_show_reports(void);
static void ui_flow_show_diagnostic(void);
static void ui_flow_reports_button_cb(lv_event_t *event);
static void ui_flow_diagnostic_button_cb(lv_event_t *event);
static void ui_flow_populate_reports_list(const char *filter, bool *out_has_match);
static void ui_flow_show_search_empty_popup(void);
static void ui_flow_cycle_finished_advance_cb(lv_event_t *event);
static void ui_flow_destroy_wifi_panel(void);
static void ui_flow_destroy_bluetooth_panel(void);
static void ui_flow_destroy_general_panel(void);
static void ui_flow_destroy_about_panel(void);
static void ui_flow_destroy_maintenance_panel(void);
static void ui_flow_oscilloscope_menu_cb(void);
static void ui_flow_show_general_panel(void);

/** @brief Cancela a transição temporária do osciloscópio para o resumo do ciclo. */
static void ui_flow_stop_cycle_finished_timer(void)
{
    if (s_ui_flow.cycle_finished_timer != NULL) {
        lv_timer_delete(s_ui_flow.cycle_finished_timer);
        s_ui_flow.cycle_finished_timer = NULL;
    }
}

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
    case UI_FLOW_MANUAL_PULSE: return 1;
    case UI_FLOW_MANUAL_CYCLES: return 0;
    case UI_FLOW_MANUAL_RPM: return 500;
    case UI_FLOW_MANUAL_PAUSE: return 0;
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

/** @brief Preenche o resumo temporário exibido após a finalização do ciclo. */
static void ui_flow_cycle_finished_populate_info(void)
{
    lv_obj_t *container = guider_ui.screen_ciclo_finalizado.container_infos;
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
    ui_flow_ready_add_info_row(container, "Tempo Total", "00:05");
    ui_flow_ready_add_info_row(container, "Ciclos Executados", "120");
    ui_flow_ready_add_info_row(container, "Pressao Media", "85.4 bar");
    ui_flow_ready_add_info_row(container, "Temperatura Media", "42 C");
}

/** @brief Finaliza a demonstração temporária e abre o resumo do ciclo. */
static void ui_flow_oscilloscope_cycle_finished_cb(bool pwm_completed)
{
    date_time_format_time(s_ui_flow.test_end_time, sizeof(s_ui_flow.test_end_time));
    (void)acquisition_stream_request_stop(!pwm_completed);
    ui_flow_show_cycle_finished();
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

/** @brief Informa se o Wi-Fi está ligado e conectado a uma rede. */
static bool ui_flow_wifi_is_connected(void)
{
    wifi_manager_status_t status = {0};
    return wifi_manager_get_status(&status) == ESP_OK && status.enabled && status.connected;
}

/** @brief Exibe o detalhe da opção geral selecionada. */
static void ui_flow_general_option_cb(lv_event_t *event)
{
    const uint8_t option = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (option == 1U && ui_flow_wifi_is_connected()) {
        return;
    }
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
    lv_obj_set_pos(row, 15, y + 5);
    ui_flow_general_style_control(row);
    lv_obj_add_event_cb(row, ui_flow_general_option_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)option);
    if (option == 1U && ui_flow_wifi_is_connected()) {
        lv_obj_add_state(row, LV_STATE_DISABLED);
    }
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
    lv_obj_set_size(s_ui_flow.general_panel, 380, 400);
    lv_obj_set_pos(s_ui_flow.general_panel, 405, 10);
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
    lv_obj_set_pos(row, 15, y + 5);
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
    lv_obj_set_size(s_ui_flow.about_panel, 385, 400);
    lv_obj_set_pos(s_ui_flow.about_panel, 405, 10);
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
    lv_obj_set_pos(row, 15, y + 5);
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
    lv_obj_set_size(s_ui_flow.maintenance_panel, 380, 400);
    lv_obj_set_pos(s_ui_flow.maintenance_panel, 405, 10);
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

/** @brief Restaura o menu principal ao detectar um toque durante o standby. */
static void ui_flow_standby_activity_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_main_menu();
}

/** @brief Atualiza os indicadores de conectividade apresentados no menu principal. */
static void ui_flow_update_menu_connectivity_labels(void)
{
    if (guider_ui.screen_menu_principal.label_status_wifi != NULL) {
        wifi_manager_status_t wifi_status = {0};
        const bool wifi_online = wifi_manager_get_status(&wifi_status) == ESP_OK && wifi_status.connected;
        if (guider_ui.screen_menu_principal.led_wifi != NULL) {
            lv_led_set_color(guider_ui.screen_menu_principal.led_wifi,
                             wifi_online ? lv_color_hex(0x00c853) : lv_color_hex(0xef1212));
        }
    }

    if (guider_ui.screen_menu_principal.label_status_bluetooth != NULL) {
        bluetooth_manager_status_t bluetooth_status = {0};
        const bool bluetooth_online = bluetooth_manager_get_status(&bluetooth_status) == ESP_OK &&
                                      bluetooth_status.enabled;
        if (guider_ui.screen_menu_principal.led_bluetooth != NULL) {
            lv_led_set_color(guider_ui.screen_menu_principal.led_bluetooth,
                             bluetooth_online ? lv_color_hex(0x00c853) : lv_color_hex(0xef1212));
        }
    }

    if (guider_ui.screen_menu_principal.led_power_control != NULL) {
        lv_led_set_color(guider_ui.screen_menu_principal.led_power_control,
                         acquisition_stream_is_power_control_online() ? lv_color_hex(0x00c853) :
                                                                        lv_color_hex(0xef1212));
    }
}

/** @brief Atualiza os labels de data, hora e conectividade enquanto o menu está ativo. */
static void ui_flow_menu_clock_update_cb(lv_timer_t *timer)
{
    (void)timer;
    if (date_time_is_synchronized() && guider_ui.screen_menu_principal.label_menu_principal_hora != NULL &&
        guider_ui.screen_menu_principal.label_menu_principal_data != NULL) {
        char time_text[6] = {0};
        char date_text[11] = {0};
        date_time_format_time(time_text, sizeof(time_text));
        date_time_format_date(date_text, sizeof(date_text));
        lv_label_set_text(guider_ui.screen_menu_principal.label_menu_principal_hora, time_text);
        lv_label_set_text(guider_ui.screen_menu_principal.label_menu_principal_data, date_text);
    }
    ui_flow_update_menu_connectivity_labels();
    if (lv_display_get_inactive_time(NULL) >= UI_FLOW_STANDBY_TIMEOUT_MS) {
        ui_flow_show_standby();
    }
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
 * @brief Alterna a exibição da senha digitada no teclado de credenciais Wi-Fi.
 *
 * @param[in] event Evento de toque do botão Mostrar ou Esconder.
 */
static void ui_flow_wifi_password_visibility_cb(lv_event_t *event)
{
    if (s_ui_flow.wifi_password == NULL) {
        return;
    }
    const bool was_hidden = lv_textarea_get_password_mode(s_ui_flow.wifi_password);
    lv_textarea_set_password_mode(s_ui_flow.wifi_password, !was_hidden);
    lv_obj_t *label = lv_event_get_user_data(event);
    if (label != NULL) {
        lv_label_set_text(label, was_hidden ? "Esconder" : "Mostrar");
    }
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
    lv_obj_t *visibility_button = lv_button_create(s_ui_flow.wifi_keyboard);
    lv_obj_set_size(visibility_button, 120, 42);
    lv_obj_set_pos(visibility_button, 530, 27);
    lv_obj_set_style_bg_color(visibility_button, lv_color_hex(0x303638), LV_PART_MAIN);
    lv_obj_t *visibility_label = lv_label_create(visibility_button);
    lv_label_set_text(visibility_label, "Mostrar");
    lv_obj_set_style_text_color(visibility_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(visibility_label);
    lv_obj_add_event_cb(visibility_button, ui_flow_wifi_password_visibility_cb,
                        LV_EVENT_CLICKED, visibility_label);
    lv_obj_t *keyboard = lv_keyboard_create(s_ui_flow.wifi_keyboard);
    lv_obj_set_size(keyboard, 760, 220);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(keyboard, s_ui_flow.wifi_password);
    lv_obj_add_event_cb(keyboard, ui_flow_wifi_connect_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, ui_flow_wifi_keyboard_cancel_cb, LV_EVENT_CANCEL, NULL);
}

/** @brief Fecha o teclado de edição dos dados do cliente. */
static void ui_flow_close_client_keyboard(void)
{
    if (s_ui_flow.client_keyboard != NULL) {
        lv_obj_delete(s_ui_flow.client_keyboard);
        s_ui_flow.client_keyboard = NULL;
        s_ui_flow.client_textarea = NULL;
    }
}

/** @brief Atualiza na tela o valor salvo para um campo de dados do cliente. */
static void ui_flow_update_client_field(ui_flow_client_field_t field)
{
    if (field < UI_FLOW_CLIENT_FIELD_COUNT && s_ui_flow.client_value_labels[field] != NULL) {
        lv_label_set_text(s_ui_flow.client_value_labels[field], s_ui_flow.client_values[field]);
    }
}

/** @brief Fecha o calendário usado para escolher a data do atendimento. */
static void ui_flow_close_client_date_calendar(void)
{
    if (s_ui_flow.client_date_popup != NULL) {
        lv_obj_delete(s_ui_flow.client_date_popup);
        s_ui_flow.client_date_popup = NULL;
        s_ui_flow.client_date_calendar = NULL;
    }
}

/**
 * @brief Registra a data pressionada no calendário de dados do cliente.
 *
 * @param[in] event Evento de alteração do calendário LVGL.
 */
static void ui_flow_client_date_calendar_cb(lv_event_t *event)
{
    lv_calendar_date_t date = {0};
    lv_obj_t *calendar = lv_event_get_current_target(event);
    if (lv_event_get_target_obj(event) != lv_calendar_get_btnmatrix(calendar)) {
        return;
    }
    if (lv_calendar_get_pressed_date(calendar, &date) == LV_RESULT_OK) {
        s_ui_flow.client_selected_date = date;
        s_ui_flow.client_highlighted_date = date;
        lv_calendar_set_highlighted_dates(calendar, &s_ui_flow.client_highlighted_date, 1);
    }
}

/**
 * @brief Confirma a data escolhida e atualiza o container correspondente.
 *
 * @param[in] event Evento de toque do botão Aplicar.
 */
static void ui_flow_client_date_apply_cb(lv_event_t *event)
{
    (void)event;
    if (s_ui_flow.client_selected_date.year == 0 && s_ui_flow.client_date_calendar != NULL) {
        s_ui_flow.client_selected_date = *lv_calendar_get_showed_date(s_ui_flow.client_date_calendar);
    }
    if (s_ui_flow.client_selected_date.year != 0) {
        lv_snprintf(s_ui_flow.client_values[UI_FLOW_CLIENT_DATE],
                    sizeof(s_ui_flow.client_values[UI_FLOW_CLIENT_DATE]), "%02u/%02u/%04u",
                    (unsigned)s_ui_flow.client_selected_date.day,
                    (unsigned)s_ui_flow.client_selected_date.month,
                    (unsigned)s_ui_flow.client_selected_date.year);
        ui_flow_update_client_field(UI_FLOW_CLIENT_DATE);
    }
    ui_flow_close_client_date_calendar();
}

/**
 * @brief Fecha o calendário sem alterar a data do atendimento.
 *
 * @param[in] event Evento de toque do botão Cancelar.
 */
static void ui_flow_client_date_cancel_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_client_date_calendar();
}

/** @brief Exibe um calendário local para escolher somente a data do atendimento. */
static void ui_flow_show_client_date_calendar(void)
{
    const time_t now = time(NULL);
    struct tm local_time = {0};
    localtime_r(&now, &local_time);
    if (s_ui_flow.client_selected_date.year == 0) {
        s_ui_flow.client_selected_date = (lv_calendar_date_t){
            .year = (uint16_t)(local_time.tm_year + 1900),
            .month = (uint8_t)(local_time.tm_mon + 1),
            .day = (uint8_t)local_time.tm_mday};
    }
    s_ui_flow.client_highlighted_date = s_ui_flow.client_selected_date;
    ui_flow_close_client_date_calendar();
    s_ui_flow.client_date_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.client_date_popup, 360, 350);
    lv_obj_center(s_ui_flow.client_date_popup);
    lv_obj_set_style_bg_color(s_ui_flow.client_date_popup, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.client_date_popup, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.client_date_popup, 2, LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(s_ui_flow.client_date_popup);
    lv_label_set_text(title, "Data");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_pos(title, 16, 12);
    s_ui_flow.client_date_calendar = lv_calendar_create(s_ui_flow.client_date_popup);
    lv_obj_set_size(s_ui_flow.client_date_calendar, 330, 245);
    lv_obj_set_pos(s_ui_flow.client_date_calendar, 15, 38);
    lv_calendar_set_today_date(s_ui_flow.client_date_calendar, (uint16_t)(local_time.tm_year + 1900),
                               (uint8_t)(local_time.tm_mon + 1), (uint8_t)local_time.tm_mday);
    lv_calendar_set_month_shown(s_ui_flow.client_date_calendar, s_ui_flow.client_selected_date.year,
                                s_ui_flow.client_selected_date.month);
    lv_calendar_set_highlighted_dates(s_ui_flow.client_date_calendar,
                                      &s_ui_flow.client_highlighted_date, 1);
    lv_obj_t *header = lv_calendar_add_header_dropdown(s_ui_flow.client_date_calendar);
    lv_calendar_header_dropdown_set_year_list(s_ui_flow.client_date_calendar, s_ui_flow_calendar_years);
    lv_obj_send_event(header, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_ui_flow.client_date_calendar, ui_flow_client_date_calendar_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *cancel = lv_button_create(s_ui_flow.client_date_popup);
    lv_obj_set_size(cancel, 120, 40);
    lv_obj_set_pos(cancel, 35, 298);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x8b1e1e), LV_PART_MAIN);
    lv_obj_add_event_cb(cancel, ui_flow_client_date_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "Cancelar");
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(cancel_label);
    lv_obj_t *apply = lv_button_create(s_ui_flow.client_date_popup);
    lv_obj_set_size(apply, 120, 40);
    lv_obj_set_pos(apply, 205, 298);
    lv_obj_set_style_bg_color(apply, lv_color_hex(0x09572e), LV_PART_MAIN);
    lv_obj_add_event_cb(apply, ui_flow_client_date_apply_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *apply_label = lv_label_create(apply);
    lv_label_set_text(apply_label, "Aplicar");
    lv_obj_set_style_text_color(apply_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(apply_label);
}

/** @brief Fecha o popup de resultado ou confirmação do relatório. */
static void ui_flow_close_report_popup(void)
{
    if (s_ui_flow.report_popup != NULL) {
        lv_obj_delete(s_ui_flow.report_popup);
        s_ui_flow.report_popup = NULL;
    }
}

/**
 * @brief Fecha o status da gravação e abre o relatório quando o salvamento teve sucesso.
 *
 * @param[in] event Evento de toque do botão OK.
 */
static void ui_flow_report_status_ok_cb(lv_event_t *event)
{
    const bool saved = (bool)(uintptr_t)lv_event_get_user_data(event);
    ui_flow_close_report_popup();
    if (saved) {
        ui_flow_show_final_report();
    }
}

/**
 * @brief Mostra o resultado do salvamento do relatório na flash.
 *
 * @param[in] saved Indica se o registro foi persistido com sucesso.
 */
static void ui_flow_show_report_status_popup(bool saved)
{
    ui_flow_close_report_popup();
    s_ui_flow.report_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.report_popup, 380, 155);
    lv_obj_center(s_ui_flow.report_popup);
    lv_obj_set_style_bg_color(s_ui_flow.report_popup, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.report_popup,
                                  lv_color_hex(saved ? 0x2cb359 : 0xc62828), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.report_popup, 2, LV_PART_MAIN);
    lv_obj_t *message = lv_label_create(s_ui_flow.report_popup);
    lv_label_set_text(message, saved ? "Relatorio salvo com sucesso." : "Falha ao salvar relatorio.");
    lv_obj_set_style_text_color(message, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *ok = lv_button_create(s_ui_flow.report_popup);
    lv_obj_set_size(ok, 110, 38);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(ok, lv_color_hex(saved ? 0x09572e : 0x8b1e1e), LV_PART_MAIN);
    lv_obj_add_event_cb(ok, ui_flow_report_status_ok_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)saved);
    lv_obj_t *label = lv_label_create(ok);
    lv_label_set_text(label, "OK");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(label);
}

/** @brief Verifica se o nome obrigatório do cliente contém ao menos um caractere visível. */
static bool ui_flow_client_name_is_empty(void)
{
    const char *name = s_ui_flow.client_values[UI_FLOW_CLIENT_NAME];
    while (*name != '\0') {
        if (!isspace((unsigned char)*name)) {
            return false;
        }
        name++;
    }
    return true;
}

/**
 * @brief Fecha o aviso de preenchimento obrigatório do nome do cliente.
 *
 * @param[in] event Evento de toque do botão OK.
 */
static void ui_flow_required_name_ok_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_report_popup();
}

/** @brief Informa que o relatório exige o nome do cliente antes de ser salvo. */
static void ui_flow_show_required_name_popup(void)
{
    ui_flow_close_report_popup();
    s_ui_flow.report_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.report_popup, 390, 155);
    lv_obj_center(s_ui_flow.report_popup);
    lv_obj_set_style_bg_color(s_ui_flow.report_popup, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.report_popup, lv_color_hex(0xc62828), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.report_popup, 2, LV_PART_MAIN);
    lv_obj_t *message = lv_label_create(s_ui_flow.report_popup);
    lv_label_set_text(message, "Informe o Nome do Cliente.");
    lv_obj_set_style_text_color(message, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *ok = lv_button_create(s_ui_flow.report_popup);
    lv_obj_set_size(ok, 100, 36);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(ok, ui_flow_required_name_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(ok);
    lv_label_set_text(label, "OK");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(label);
}

/**
 * @brief Salva os dados atuais do ciclo e do cliente no armazenamento de relatórios.
 *
 * @param[in] event Evento de toque do botão Salvar e Gerar Relatorio.
 */
static void ui_flow_save_client_report_cb(lv_event_t *event)
{
    (void)event;
    if (ui_flow_client_name_is_empty()) {
        ui_flow_show_required_name_popup();
        return;
    }
    report_storage_record_t report = {0};
    if (s_ui_flow.client_values[UI_FLOW_CLIENT_DATE][0] == '\0') {
        date_time_format_date(s_ui_flow.client_values[UI_FLOW_CLIENT_DATE],
                              sizeof(s_ui_flow.client_values[UI_FLOW_CLIENT_DATE]));
        ui_flow_update_client_field(UI_FLOW_CLIENT_DATE);
    }
    strncpy(report.injector_type, s_ui_flow.injector_type == UI_FLOW_INJECTOR_75V_GDI ?
            "Bico GDI" : "12V Bico Comum", sizeof(report.injector_type) - 1U);
    strncpy(report.test_description, s_ui_flow.selected_test_description,
            sizeof(report.test_description) - 1U);
    strncpy(report.date, s_ui_flow.client_values[UI_FLOW_CLIENT_DATE], sizeof(report.date) - 1U);
    strncpy(report.start_time, s_ui_flow.test_start_time, sizeof(report.start_time) - 1U);
    strncpy(report.end_time, s_ui_flow.test_end_time, sizeof(report.end_time) - 1U);
    strncpy(report.customer_name, s_ui_flow.client_values[UI_FLOW_CLIENT_NAME], sizeof(report.customer_name) - 1U);
    strncpy(report.phone, s_ui_flow.client_values[UI_FLOW_CLIENT_PHONE], sizeof(report.phone) - 1U);
    strncpy(report.vehicle, s_ui_flow.client_values[UI_FLOW_CLIENT_VEHICLE], sizeof(report.vehicle) - 1U);
    strncpy(report.plate, s_ui_flow.client_values[UI_FLOW_CLIENT_PLATE], sizeof(report.plate) - 1U);
    strncpy(report.mileage, s_ui_flow.client_values[UI_FLOW_CLIENT_KM], sizeof(report.mileage) - 1U);
    strncpy(report.observations, s_ui_flow.client_values[UI_FLOW_CLIENT_OBSERVATIONS], sizeof(report.observations) - 1U);
    report.pressure_bar = (uint16_t)s_ui_flow.manual_pressure;
    report.pulse_ms = (uint16_t)s_ui_flow.manual_pulse;
    report.rpm = (uint32_t)s_ui_flow.manual_rpm;
    report.cycles_executed = (uint32_t)s_ui_flow.manual_cycles;
    report.total_seconds = 5;
    uint32_t sequence = 0;
    const bool saved = report_storage_save(&report, &sequence) == ESP_OK;
    if (saved) {
        report.sequence = sequence;
        s_ui_flow.selected_report = report;
        s_ui_flow.selected_report_valid = true;
        s_ui_flow.final_report_from_list = false;
    }
    ui_flow_show_report_status_popup(saved);
}

/**
 * @brief Confirma o cancelamento do relatório e retorna ao menu principal.
 *
 * @param[in] event Evento de toque do botão SIM.
 */
static void ui_flow_cancel_report_confirm_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_report_popup();
    ui_flow_show_main_menu();
}

/**
 * @brief Fecha a confirmação de cancelamento e mantém os dados em edição.
 *
 * @param[in] event Evento de toque do botão CANCELAR.
 */
static void ui_flow_cancel_report_abort_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_report_popup();
}

/**
 * @brief Solicita confirmação antes de descartar os dados do relatório atual.
 *
 * @param[in] event Evento de toque do botão Cancelar.
 */
static void ui_flow_cancel_report_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_report_popup();
    s_ui_flow.report_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.report_popup, 400, 175);
    lv_obj_center(s_ui_flow.report_popup);
    lv_obj_set_style_bg_color(s_ui_flow.report_popup, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.report_popup, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.report_popup, 2, LV_PART_MAIN);
    lv_obj_t *message = lv_label_create(s_ui_flow.report_popup);
    lv_label_set_text(message, "Deseja cancelar o relatorio?");
    lv_obj_set_style_text_color(message, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *yes = lv_button_create(s_ui_flow.report_popup);
    lv_obj_set_size(yes, 120, 40);
    lv_obj_set_pos(yes, 55, 112);
    lv_obj_set_style_bg_color(yes, lv_color_hex(0x8b1e1e), LV_PART_MAIN);
    lv_obj_add_event_cb(yes, ui_flow_cancel_report_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *yes_label = lv_label_create(yes);
    lv_label_set_text(yes_label, "SIM");
    lv_obj_set_style_text_color(yes_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(yes_label);
    lv_obj_t *abort = lv_button_create(s_ui_flow.report_popup);
    lv_obj_set_size(abort, 120, 40);
    lv_obj_set_pos(abort, 225, 112);
    lv_obj_set_style_bg_color(abort, lv_color_hex(0x303638), LV_PART_MAIN);
    lv_obj_add_event_cb(abort, ui_flow_cancel_report_abort_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *abort_label = lv_label_create(abort);
    lv_label_set_text(abort_label, "CANCELAR");
    lv_obj_set_style_text_color(abort_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(abort_label);
}

/**
 * @brief Confirma o texto digitado para o campo de dados do cliente ativo.
 *
 * @param[in] event Evento de confirmação do teclado LVGL.
 */
static void ui_flow_client_keyboard_ready_cb(lv_event_t *event)
{
    (void)event;
    if (s_ui_flow.client_textarea != NULL && s_ui_flow.client_active_field < UI_FLOW_CLIENT_FIELD_COUNT) {
        strncpy(s_ui_flow.client_values[s_ui_flow.client_active_field],
                lv_textarea_get_text(s_ui_flow.client_textarea),
                sizeof(s_ui_flow.client_values[s_ui_flow.client_active_field]) - 1U);
        s_ui_flow.client_values[s_ui_flow.client_active_field]
            [sizeof(s_ui_flow.client_values[s_ui_flow.client_active_field]) - 1U] = '\0';
        ui_flow_update_client_field(s_ui_flow.client_active_field);
    }
    ui_flow_close_client_keyboard();
}

/**
 * @brief Cancela a edição atual dos dados do cliente sem alterar o valor salvo.
 *
 * @param[in] event Evento de cancelamento do teclado LVGL.
 */
static void ui_flow_client_keyboard_cancel_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_client_keyboard();
}

/**
 * @brief Abre o teclado para editar um dos campos de dados do cliente.
 *
 * @param[in] field Campo selecionado pelo usuário.
 */
static void ui_flow_show_client_keyboard(ui_flow_client_field_t field)
{
    static const char *const field_titles[UI_FLOW_CLIENT_FIELD_COUNT] = {
        "Nome do Cliente", "Data", "Telefone", "Veiculo", "Placa", "Km", "Observacoes"
    };
    if (field >= UI_FLOW_CLIENT_FIELD_COUNT) {
        return;
    }
    ui_flow_close_client_keyboard();
    s_ui_flow.client_active_field = field;
    s_ui_flow.client_keyboard = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.client_keyboard, 800, 350);
    lv_obj_set_pos(s_ui_flow.client_keyboard, 0, 130);
    lv_obj_set_style_bg_color(s_ui_flow.client_keyboard, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.client_keyboard, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.client_keyboard, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(s_ui_flow.client_keyboard);
    lv_label_set_text(title, field_titles[field]);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_pos(title, 16, 5);
    s_ui_flow.client_textarea = lv_textarea_create(s_ui_flow.client_keyboard);
    lv_obj_set_size(s_ui_flow.client_textarea, 500, 42);
    lv_obj_set_pos(s_ui_flow.client_textarea, 16, 27);
    lv_textarea_set_one_line(s_ui_flow.client_textarea, true);
    lv_textarea_set_text(s_ui_flow.client_textarea, s_ui_flow.client_values[field]);
    lv_obj_t *keyboard = lv_keyboard_create(s_ui_flow.client_keyboard);
    lv_obj_set_size(keyboard, 760, 220);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    if (field == UI_FLOW_CLIENT_PHONE || field == UI_FLOW_CLIENT_KM) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
    }
    lv_keyboard_set_textarea(keyboard, s_ui_flow.client_textarea);
    lv_obj_add_event_cb(keyboard, ui_flow_client_keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, ui_flow_client_keyboard_cancel_cb, LV_EVENT_CANCEL, NULL);
}

/**
 * @brief Abre o teclado correspondente ao container de dados tocado.
 *
 * @param[in] event Evento de toque do container.
 */
static void ui_flow_client_field_click_cb(lv_event_t *event)
{
    const ui_flow_client_field_t field = (ui_flow_client_field_t)(uintptr_t)lv_event_get_user_data(event);
    if (field == UI_FLOW_CLIENT_DATE) {
        ui_flow_show_client_date_calendar();
        return;
    }
    ui_flow_show_client_keyboard(field);
}

/**
 * @brief Cria os textos fixo e editável de um container de dados do cliente.
 *
 * @param[in] container Container exportado pelo GUI Guider.
 * @param[in] title Texto fixo alinhado à esquerda.
 * @param[in] field Campo que será editado ao tocar no container.
 */
static void ui_flow_setup_client_field(lv_obj_t *container, const char *title, ui_flow_client_field_t field)
{
    if (container == NULL || field >= UI_FLOW_CLIENT_FIELD_COUNT) {
        return;
    }
    lv_obj_set_style_pad_left(container, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(container, 12, LV_PART_MAIN);
    if (field == UI_FLOW_CLIENT_OBSERVATIONS) {
        lv_obj_set_scroll_dir(container, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);
    } else {
        lv_obj_set_scroll_dir(container, LV_DIR_NONE);
        lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_t *title_label = lv_label_create(container);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *value_label = lv_label_create(container);
    lv_obj_set_width(value_label, 210);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_label_set_text(value_label, s_ui_flow.client_values[field]);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);
    s_ui_flow.client_value_labels[field] = value_label;
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(container, ui_flow_client_field_click_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)field);
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
    lv_obj_set_size(s_ui_flow.bluetooth_panel, 380, 400);
    lv_obj_set_pos(s_ui_flow.bluetooth_panel, 405, 10);
    lv_obj_set_style_bg_color(s_ui_flow.bluetooth_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui_flow.bluetooth_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.bluetooth_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.bluetooth_panel, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui_flow.bluetooth_panel, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui_flow.bluetooth_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui_flow.bluetooth_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = lv_button_create(s_ui_flow.bluetooth_panel);
    lv_obj_set_size(header, 340, 50);
    lv_obj_set_pos(header, 15, 18);
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
    lv_obj_set_pos(s_ui_flow.bluetooth_status, 16, 77);
    lv_obj_set_style_text_color(s_ui_flow.bluetooth_status, lv_color_hex(0xffffff), LV_PART_MAIN);
    s_ui_flow.bluetooth_device_list = lv_obj_create(s_ui_flow.bluetooth_panel);
    lv_obj_set_size(s_ui_flow.bluetooth_device_list, 340, 270);
    lv_obj_align(s_ui_flow.bluetooth_device_list, LV_ALIGN_TOP_MID, 0, 99);
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
    lv_obj_set_size(s_ui_flow.wifi_panel, 380, 400);
    lv_obj_set_pos(s_ui_flow.wifi_panel, 405, 10);
    lv_obj_set_style_bg_color(s_ui_flow.wifi_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui_flow.wifi_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.wifi_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.wifi_panel, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui_flow.wifi_panel, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui_flow.wifi_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui_flow.wifi_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_button_create(s_ui_flow.wifi_panel);
    lv_obj_set_size(header, 340, 50);
    lv_obj_set_pos(header, 15, 18);
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
    lv_obj_set_pos(s_ui_flow.wifi_connection_label, 16, 75);
    lv_obj_set_style_text_color(s_ui_flow.wifi_connection_label, lv_color_hex(0x00c853), LV_PART_MAIN);
    s_ui_flow.wifi_status = lv_label_create(s_ui_flow.wifi_panel);
    lv_obj_set_pos(s_ui_flow.wifi_status, 116, 75);
    lv_obj_set_style_text_color(s_ui_flow.wifi_status, lv_color_hex(0xffffff), LV_PART_MAIN);
    s_ui_flow.wifi_network_list = lv_obj_create(s_ui_flow.wifi_panel);
    lv_obj_set_size(s_ui_flow.wifi_network_list, 340, 270);
    lv_obj_align(s_ui_flow.wifi_network_list, LV_ALIGN_TOP_MID, 0, 99);
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
 * @brief Abre a lista de relatórios persistidos.
 *
 * @param[in] event Evento de toque do botão Relatorios.
 */
static void ui_flow_reports_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_reports();
}

/**
 * @brief Abre a tela de diagnóstico do equipamento.
 *
 * @param[in] event Evento de toque do botão Diagnostico.
 */
static void ui_flow_diagnostic_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_diagnostic();
}

/** @brief Retorna o objeto raiz de um card a partir de seu identificador persistido. */
static lv_obj_t *ui_flow_get_menu_button(ui_flow_menu_item_t item)
{
    switch (item) {
    case UI_FLOW_MENU_CLEANING: return guider_ui.screen_menu_principal.button_limpeza_bico;
    case UI_FLOW_MENU_TEST_MODE: return guider_ui.screen_menu_principal.button_modo_teste;
    case UI_FLOW_MENU_REPORTS: return guider_ui.screen_menu_principal.button_relatorio;
    case UI_FLOW_MENU_DIAGNOSTIC: return guider_ui.screen_menu_principal.button_diagnostico;
    case UI_FLOW_MENU_SETTINGS: return guider_ui.screen_menu_principal.button_configuracoes;
    default: return NULL;
    }
}

/** @brief Retorna o identificador persistido correspondente a um card do menu. */
static ui_flow_menu_item_t ui_flow_get_menu_item(const lv_obj_t *button)
{
    for (uint32_t item = 0; item < UI_FLOW_MENU_ITEM_COUNT; item++) {
        if (ui_flow_get_menu_button((ui_flow_menu_item_t)item) == button) {
            return (ui_flow_menu_item_t)item;
        }
    }
    return UI_FLOW_MENU_ITEM_COUNT;
}

/** @brief Verifica se uma ordem de slots contém cada card uma única vez. */
static bool ui_flow_menu_order_is_valid(const uint8_t *order)
{
    bool present[UI_FLOW_MENU_ITEM_COUNT] = {0};
    for (uint32_t slot = 0; slot < UI_FLOW_MENU_ITEM_COUNT; slot++) {
        if (order[slot] >= UI_FLOW_MENU_ITEM_COUNT || present[order[slot]]) {
            return false;
        }
        present[order[slot]] = true;
    }
    return true;
}

/** @brief Persiste a ordem atual dos cards do menu na NVS. */
static void ui_flow_save_menu_layout(void)
{
    nvs_handle_t handle = 0;
    if (nvs_open(UI_FLOW_MENU_LAYOUT_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(handle, UI_FLOW_MENU_LAYOUT_KEY, s_ui_flow.menu_slot_order,
                     sizeof(s_ui_flow.menu_slot_order)) == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

/** @brief Carrega a ordem persistida ou restaura a disposição padrão quando não houver uma válida. */
static void ui_flow_load_menu_layout(void)
{
    if (s_ui_flow.menu_layout_loaded) {
        return;
    }
    size_t size = sizeof(s_ui_flow.menu_slot_order);
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(UI_FLOW_MENU_LAYOUT_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_get_blob(handle, UI_FLOW_MENU_LAYOUT_KEY, s_ui_flow.menu_slot_order, &size);
        nvs_close(handle);
    }
    if (error != ESP_OK || size != sizeof(s_ui_flow.menu_slot_order) ||
        !ui_flow_menu_order_is_valid(s_ui_flow.menu_slot_order)) {
        memcpy(s_ui_flow.menu_slot_order, s_ui_flow_default_menu_order,
               sizeof(s_ui_flow.menu_slot_order));
        ui_flow_save_menu_layout();
    }
    s_ui_flow.menu_layout_loaded = true;
}

/** @brief Centraliza localmente o ícone e o texto de um card após a mudança de tamanho. */
static void ui_flow_align_menu_card_content(ui_flow_menu_item_t item)
{
    lv_obj_t *image = NULL;
    lv_obj_t *label = NULL;
    switch (item) {
    case UI_FLOW_MENU_CLEANING:
        image = guider_ui.screen_menu_principal.button_limpeza_bico_image_bt_limpeza_bico;
        label = guider_ui.screen_menu_principal.button_limpeza_bico_label_bt_limpeza_bico;
        break;
    case UI_FLOW_MENU_TEST_MODE:
        image = guider_ui.screen_menu_principal.button_modo_teste_image_bt_modo_teste;
        label = guider_ui.screen_menu_principal.button_modo_teste_label_bt_modo_teste;
        break;
    case UI_FLOW_MENU_REPORTS:
        image = guider_ui.screen_menu_principal.button_relatorio_image_bt_relatorio;
        label = guider_ui.screen_menu_principal.button_relatorio_label_bt_relatorios;
        break;
    case UI_FLOW_MENU_DIAGNOSTIC:
        image = guider_ui.screen_menu_principal.button_diagnostico_image_bt_diagnostico;
        label = guider_ui.screen_menu_principal.button_diagnostico_label_bt_diagnosticos;
        break;
    case UI_FLOW_MENU_SETTINGS:
        image = guider_ui.screen_menu_principal.button_configuracoes_image_bt_configuracoes;
        label = guider_ui.screen_menu_principal.button_configuracoes_label_bt_configuracoes;
        break;
    default: return;
    }
    if (image != NULL) {
        lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 0);
    }
    if (label != NULL) {
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -18);
    }
}

/** @brief Aplica a geometria de cada slot ao card que ocupa a posição correspondente. */
static void ui_flow_apply_menu_layout(void)
{
    ui_flow_load_menu_layout();
    for (uint32_t slot = 0; slot < UI_FLOW_MENU_ITEM_COUNT; slot++) {
        const ui_flow_menu_item_t item = (ui_flow_menu_item_t)s_ui_flow.menu_slot_order[slot];
        lv_obj_t *button = ui_flow_get_menu_button(item);
        if (button == NULL) {
            continue;
        }
        lv_obj_set_size(button, s_ui_flow_menu_slots[slot].width, s_ui_flow_menu_slots[slot].height);
        lv_obj_set_pos(button, s_ui_flow_menu_slots[slot].x, s_ui_flow_menu_slots[slot].y);
        ui_flow_align_menu_card_content(item);
    }
}

/** @brief Troca os slots ocupados por dois cards e persiste a nova organização. */
static void ui_flow_swap_menu_slots(lv_obj_t *first_button, lv_obj_t *second_button)
{
    const ui_flow_menu_item_t first = ui_flow_get_menu_item(first_button);
    const ui_flow_menu_item_t second = ui_flow_get_menu_item(second_button);
    if (first >= UI_FLOW_MENU_ITEM_COUNT || second >= UI_FLOW_MENU_ITEM_COUNT || first == second) {
        return;
    }
    uint32_t first_slot = UI_FLOW_MENU_ITEM_COUNT;
    uint32_t second_slot = UI_FLOW_MENU_ITEM_COUNT;
    for (uint32_t slot = 0; slot < UI_FLOW_MENU_ITEM_COUNT; slot++) {
        if (s_ui_flow.menu_slot_order[slot] == first) {
            first_slot = slot;
        }
        if (s_ui_flow.menu_slot_order[slot] == second) {
            second_slot = slot;
        }
    }
    if (first_slot >= UI_FLOW_MENU_ITEM_COUNT || second_slot >= UI_FLOW_MENU_ITEM_COUNT) {
        return;
    }
    const uint8_t temp = s_ui_flow.menu_slot_order[first_slot];
    s_ui_flow.menu_slot_order[first_slot] = s_ui_flow.menu_slot_order[second_slot];
    s_ui_flow.menu_slot_order[second_slot] = temp;
    ui_flow_apply_menu_layout();
    ui_flow_save_menu_layout();
}

/** @brief Informa se um ponto absoluto da tela está dentro de um objeto LVGL. */
static bool ui_flow_menu_point_in_object(const lv_obj_t *object, int32_t x, int32_t y)
{
    if (object == NULL) {
        return false;
    }
    lv_area_t area = {0};
    lv_obj_get_coords(object, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

/** @brief Retorna o card do menu principal localizado sob um ponto da tela. */
static lv_obj_t *ui_flow_menu_button_at(int32_t x, int32_t y, const lv_obj_t *ignored)
{
    lv_obj_t *const buttons[] = {
        guider_ui.screen_menu_principal.button_configuracoes,
        guider_ui.screen_menu_principal.button_diagnostico,
        guider_ui.screen_menu_principal.button_relatorio,
        guider_ui.screen_menu_principal.button_modo_teste,
        guider_ui.screen_menu_principal.button_limpeza_bico,
    };
    for (uint32_t index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++) {
        if (buttons[index] != ignored && ui_flow_menu_point_in_object(buttons[index], x, y)) {
            return buttons[index];
        }
    }
    return NULL;
}

/** @brief Retorna o card que está coberto em pelo menos metade pelo card arrastado. */
static lv_obj_t *ui_flow_menu_overlap_target(const lv_obj_t *source)
{
    if (source == NULL) {
        return NULL;
    }
    lv_area_t source_area = {0};
    lv_obj_get_coords(source, &source_area);
    const int32_t source_area_size = (source_area.x2 - source_area.x1 + 1) *
                                     (source_area.y2 - source_area.y1 + 1);
    lv_obj_t *const buttons[] = {
        guider_ui.screen_menu_principal.button_configuracoes,
        guider_ui.screen_menu_principal.button_diagnostico,
        guider_ui.screen_menu_principal.button_relatorio,
        guider_ui.screen_menu_principal.button_modo_teste,
        guider_ui.screen_menu_principal.button_limpeza_bico,
    };
    for (uint32_t index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++) {
        if (buttons[index] == NULL || buttons[index] == source) {
            continue;
        }
        lv_area_t candidate_area = {0};
        lv_obj_get_coords(buttons[index], &candidate_area);
        const int32_t left = source_area.x1 > candidate_area.x1 ? source_area.x1 : candidate_area.x1;
        const int32_t top = source_area.y1 > candidate_area.y1 ? source_area.y1 : candidate_area.y1;
        const int32_t right = source_area.x2 < candidate_area.x2 ? source_area.x2 : candidate_area.x2;
        const int32_t bottom = source_area.y2 < candidate_area.y2 ? source_area.y2 : candidate_area.y2;
        if (right >= left && bottom >= top &&
            (right - left + 1) * (bottom - top + 1) * 2 >= source_area_size) {
            return buttons[index];
        }
    }
    return NULL;
}

/** @brief Finaliza uma reorganização e troca os cards se houver um destino válido. */
static void ui_flow_finish_menu_drag(void)
{
    lv_obj_t *source = s_ui_flow.menu_drag_source;
    lv_obj_t *target = s_ui_flow.menu_drag_target;
    if (source != NULL) {
        if (s_ui_flow.menu_dragging && target != NULL) {
            ui_flow_swap_menu_slots(source, target);
        } else {
            ui_flow_apply_menu_layout();
        }
        lv_obj_add_flag(source, LV_OBJ_FLAG_CLICKABLE);
    }
    s_ui_flow.menu_drag_source = NULL;
    s_ui_flow.menu_drag_target = NULL;
    s_ui_flow.menu_dragging = false;
}

/**
 * @brief Detecta e executa a reorganização de cards com dois dedos no menu principal.
 *
 * @param[in] touch_data Pontos de toque brutos mais recentes do GT911.
 */
void ui_flow_handle_multitouch(const gt911_touch_data_t *touch_data)
{
    if (touch_data == NULL) {
        return;
    }
    if (lv_screen_active() != guider_ui.screen_menu_principal.screen) {
        s_ui_flow.menu_drag_source = NULL;
        s_ui_flow.menu_drag_target = NULL;
        s_ui_flow.menu_dragging = false;
        return;
    }
    if (!touch_data->touched || touch_data->points != 2U) {
        ui_flow_finish_menu_drag();
        return;
    }
    const int32_t center_x = ((int32_t)touch_data->x[0] + (int32_t)touch_data->x[1]) / 2;
    const int32_t center_y = ((int32_t)touch_data->y[0] + (int32_t)touch_data->y[1]) / 2;
    if (s_ui_flow.menu_drag_source == NULL) {
        lv_obj_t *first = ui_flow_menu_button_at(touch_data->x[0], touch_data->y[0], NULL);
        lv_obj_t *second = ui_flow_menu_button_at(touch_data->x[1], touch_data->y[1], NULL);
        if (first == NULL || first != second) {
            return;
        }
        s_ui_flow.menu_drag_source = first;
        s_ui_flow.menu_drag_start_x = center_x;
        s_ui_flow.menu_drag_start_y = center_y;
        s_ui_flow.menu_drag_source_x = lv_obj_get_x(first);
        s_ui_flow.menu_drag_source_y = lv_obj_get_y(first);
        return;
    }
    const int32_t delta_x = center_x - s_ui_flow.menu_drag_start_x;
    const int32_t delta_y = center_y - s_ui_flow.menu_drag_start_y;
    if (!s_ui_flow.menu_dragging &&
        delta_x * delta_x + delta_y * delta_y >= UI_FLOW_MENU_DRAG_THRESHOLD_PX * UI_FLOW_MENU_DRAG_THRESHOLD_PX) {
        s_ui_flow.menu_dragging = true;
        lv_obj_remove_flag(s_ui_flow.menu_drag_source, LV_OBJ_FLAG_CLICKABLE);
        lv_indev_wait_release(lv_indev_active());
    }
    if (s_ui_flow.menu_dragging) {
        lv_obj_set_pos(s_ui_flow.menu_drag_source,
                       center_x - (int32_t)lv_obj_get_width(s_ui_flow.menu_drag_source) / 2,
                       center_y - (int32_t)lv_obj_get_height(s_ui_flow.menu_drag_source) / 2);
        s_ui_flow.menu_drag_target = ui_flow_menu_overlap_target(s_ui_flow.menu_drag_source);
    }
}

/** @brief Monta os parâmetros PWM a partir do fluxo e ajustes atualmente selecionados. */
static acquisition_stream_start_config_t ui_flow_build_acquisition_config(void)
{
    const bool automatic = s_ui_flow.automatic_operation_mode >= 2U;
    const uint8_t operation_mode = automatic ? s_ui_flow.automatic_operation_mode :
                                   (s_ui_flow.manual_cycles == 0 ? 0U : 1U);
    return (acquisition_stream_start_config_t){
        .profile = osc_get_acquisition_profile(),
        .rpm = (uint16_t)s_ui_flow.manual_rpm,
        .ton_ms = (uint8_t)s_ui_flow.manual_pulse,
        .cycles = (uint16_t)(operation_mode == 0U ? 1 : s_ui_flow.manual_cycles),
        .pause_ms = (uint16_t)s_ui_flow.manual_pause,
        .operation_mode = operation_mode,
    };
}

/**
 * @brief Cria sob demanda e carrega a tela do osciloscópio.
 *
 * @param[in] event Evento LVGL da escolha do tipo de bico.
 */
static void ui_flow_oscilloscope_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_stop_cycle_finished_timer();
    ui_flow_destroy_wifi_panel();
    ui_flow_destroy_bluetooth_panel();
    if (s_ui_flow.menu_clock_timer != NULL) {
        lv_timer_delete(s_ui_flow.menu_clock_timer);
        s_ui_flow.menu_clock_timer = NULL;
    }
    osc_set_menu_callback(ui_flow_oscilloscope_menu_cb);
    osc_set_cycle_finished_callback(ui_flow_oscilloscope_cycle_finished_cb);
    if (osc_create(NULL) != ESP_OK || osc_get_screen() == NULL) {
        return;
    }
    date_time_format_time(s_ui_flow.test_start_time, sizeof(s_ui_flow.test_start_time));
    memset(s_ui_flow.test_end_time, 0, sizeof(s_ui_flow.test_end_time));
    const acquisition_stream_start_config_t config = ui_flow_build_acquisition_config();
    if (!acquisition_stream_request_start(&config)) {
        ESP_LOGW("ui_flow", "nao foi possivel solicitar inicio da aquisicao SPI");
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
    strncpy(s_ui_flow.selected_test_description,
            s_ui_flow.injector_type == UI_FLOW_INJECTOR_75V_GDI ? "Bico GDI" : "12V Bico Comum",
            sizeof(s_ui_flow.selected_test_description) - 1U);
    s_ui_flow.selected_test_description[sizeof(s_ui_flow.selected_test_description) - 1U] = '\0';
    ui_flow_show_bicos_step_two();
}

/** @brief Abre a configuração manual de parâmetros do ciclo. */
static void ui_flow_bicos_manual_button_cb(lv_event_t *event)
{
    (void)event;
    s_ui_flow.automatic_operation_mode = 0;
    ui_flow_show_bicos_config_manual();
}

/** @brief Abre a confirmação final antes de iniciar o ciclo. */
static void ui_flow_ready_to_start_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_ready_to_start();
}

/** @brief Abre a seleção de testes automáticos. */
static void ui_flow_automatic_tests_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_automatic_tests();
}

/** @brief Abre a segunda página de testes automáticos. */
static void ui_flow_automatic_tests_more_button_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_automatic_tests_second_page();
}

/**
 * @brief Registra a descrição do teste automático escolhido e inicia o osciloscópio.
 *
 * @param[in] event Evento de toque do teste automático.
 */
static void ui_flow_automatic_test_start_cb(lv_event_t *event)
{
    const ui_flow_automatic_test_t *test = lv_event_get_user_data(event);
    if (test != NULL) {
        strncpy(s_ui_flow.selected_test_description, test->description,
                sizeof(s_ui_flow.selected_test_description) - 1U);
        s_ui_flow.selected_test_description[sizeof(s_ui_flow.selected_test_description) - 1U] = '\0';
        s_ui_flow.automatic_operation_mode = test->operation_mode;
    }
    ui_flow_oscilloscope_button_cb(event);
}

/** @brief Retorna do osciloscópio ao menu sem destruir sua tela persistente. */
static void ui_flow_oscilloscope_menu_cb(void)
{
    (void)acquisition_stream_request_stop(true);
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
    ui_flow_stop_cycle_finished_timer();
    lv_display_trigger_activity(NULL);
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
    if (guider_ui.screen_menu_principal.button_relatorio != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_menu_principal.button_relatorio,
                            ui_flow_reports_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_menu_principal.button_diagnostico != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_menu_principal.button_diagnostico,
                            ui_flow_diagnostic_button_cb, LV_EVENT_CLICKED, NULL);
    }
    ui_flow_apply_menu_layout();
    s_ui_flow.menu_clock_timer = lv_timer_create(ui_flow_menu_clock_update_cb, 1000, NULL);
    ui_flow_menu_clock_update_cb(s_ui_flow.menu_clock_timer);
    lv_screen_load_anim(guider_ui.screen_menu_principal.screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, delete_previous_screen);
}

/** @brief Cria e carrega a tela exibida após um minuto sem toque no menu. */
static void ui_flow_show_standby(void)
{
    if (s_ui_flow.menu_clock_timer != NULL) {
        lv_timer_delete(s_ui_flow.menu_clock_timer);
        s_ui_flow.menu_clock_timer = NULL;
    }
    memset(&guider_ui.screen_stand_by, 0, sizeof(guider_ui.screen_stand_by));
    setup_screen_stand_by(&guider_ui);
    if (guider_ui.screen_stand_by.screen == NULL || guider_ui.screen_stand_by.image_stand_by == NULL) {
        return;
    }
    lv_obj_t *image = guider_ui.screen_stand_by.image_stand_by;
    lv_obj_add_event_cb(guider_ui.screen_stand_by.screen, ui_flow_standby_activity_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(image, ui_flow_standby_activity_cb, LV_EVENT_PRESSED, NULL);
    lv_screen_load_anim(guider_ui.screen_stand_by.screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
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
                            ui_flow_automatic_tests_button_cb, LV_EVENT_CLICKED, NULL);
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

/** @brief Cria, preenche e carrega a tela de conclusão temporária do ciclo. */
static void ui_flow_show_cycle_finished(void)
{
    memset(&guider_ui.screen_ciclo_finalizado, 0, sizeof(guider_ui.screen_ciclo_finalizado));
    setup_screen_ciclo_finalizado(&guider_ui);
    if (guider_ui.screen_ciclo_finalizado.screen == NULL) {
        return;
    }
    ui_flow_cycle_finished_populate_info();
    lv_obj_add_event_cb(guider_ui.screen_ciclo_finalizado.button_avancar,
                        ui_flow_cycle_finished_advance_cb, LV_EVENT_CLICKED, NULL);
    lv_screen_load_anim(guider_ui.screen_ciclo_finalizado.screen,
                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, false);
    osc_destroy();
}

/**
 * @brief Avança do resumo final do ciclo para o preenchimento dos dados do cliente.
 *
 * @param[in] event Evento de toque do botão Avancar.
 */
static void ui_flow_cycle_finished_advance_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_show_client_data();
}

/** @brief Cria, configura e apresenta a tela de dados do cliente. */
static void ui_flow_show_client_data(void)
{
    memset(s_ui_flow.client_values, 0, sizeof(s_ui_flow.client_values));
    s_ui_flow.client_selected_date = (lv_calendar_date_t){0};
    s_ui_flow.client_highlighted_date = (lv_calendar_date_t){0};
    memset(&guider_ui.screen_dados_cliente, 0, sizeof(guider_ui.screen_dados_cliente));
    memset(s_ui_flow.client_value_labels, 0, sizeof(s_ui_flow.client_value_labels));
    setup_screen_dados_cliente(&guider_ui);
    if (guider_ui.screen_dados_cliente.screen == NULL) {
        return;
    }
    ui_flow_setup_client_field(guider_ui.screen_dados_cliente.container_nome,
                               "Nome do Cliente*", UI_FLOW_CLIENT_NAME);
    ui_flow_setup_client_field(guider_ui.screen_dados_cliente.container_data,
                               "Data", UI_FLOW_CLIENT_DATE);
    ui_flow_setup_client_field(guider_ui.screen_dados_cliente.container_telefone,
                               "Telefone", UI_FLOW_CLIENT_PHONE);
    ui_flow_setup_client_field(guider_ui.screen_dados_cliente.container_veiculo,
                               "Veiculo", UI_FLOW_CLIENT_VEHICLE);
    ui_flow_setup_client_field(guider_ui.screen_dados_cliente.container_placa,
                               "Placa", UI_FLOW_CLIENT_PLATE);
    ui_flow_setup_client_field(guider_ui.screen_dados_cliente.container_km,
                               "Km", UI_FLOW_CLIENT_KM);
    ui_flow_setup_client_field(guider_ui.screen_dados_cliente.container_obs,
                               "Observacoes", UI_FLOW_CLIENT_OBSERVATIONS);
    lv_obj_add_event_cb(guider_ui.screen_dados_cliente.button_gerar_relatorio,
                        ui_flow_save_client_report_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(guider_ui.screen_dados_cliente.button_cancelar,
                        ui_flow_cancel_report_cb, LV_EVENT_CLICKED, NULL);
    lv_screen_load_anim(guider_ui.screen_dados_cliente.screen,
                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * @brief Preenche um container do relatório final com um resumo de texto rolável.
 *
 * @param[in] container Container exportado pelo GUI Guider.
 * @param[in] text Texto de resumo a exibir.
 */
static void ui_flow_fill_final_report_container(lv_obj_t *container, const char *text)
{
    if (container == NULL) {
        return;
    }
    lv_obj_clean(container);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_left(container, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(container, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(container, 8, LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(container);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
}

/**
 * @brief Retorna ao menu principal a partir do relatório final.
 *
 * @param[in] event Evento de toque do botão Menu Principal.
 */
static void ui_flow_final_report_menu_cb(lv_event_t *event)
{
    (void)event;
    if (s_ui_flow.final_report_from_list) {
        ui_flow_show_reports();
    } else {
        ui_flow_show_main_menu();
    }
}

/** @brief Cria e apresenta o resumo final do relatório que acabou de ser salvo. */
static void ui_flow_show_final_report(void)
{
    char customer_summary[512] = {0};
    char cycle_summary[512] = {0};
    if (!s_ui_flow.selected_report_valid) {
        return;
    }
    const report_storage_record_t *report = &s_ui_flow.selected_report;
    lv_snprintf(customer_summary, sizeof(customer_summary),
                "Data: %s\nNome: %s\nTelefone: %s\nVeiculo: %s\nPlaca: %s\nKm: %s\nObservacoes: %s",
                report->date, report->customer_name, report->phone, report->vehicle, report->plate,
                report->mileage, report->observations);
    lv_snprintf(cycle_summary, sizeof(cycle_summary),
                "Bico: %s\nTeste: %s\nHorario Inicial: %s\nHorario Termino: %s\nPressao: %u bar\nRPM: %lu\nPulso: %u ms\nCiclos: %lu\nTempo Total: %02lu:%02lu",
                report->injector_type, report->test_description, report->start_time, report->end_time,
                (unsigned)report->pressure_bar,
                (unsigned long)report->rpm, (unsigned)report->pulse_ms,
                (unsigned long)report->cycles_executed, (unsigned long)(report->total_seconds / 60U),
                (unsigned long)(report->total_seconds % 60U));
    memset(&guider_ui.screen_relatorio_final, 0, sizeof(guider_ui.screen_relatorio_final));
    setup_screen_relatorio_final(&guider_ui);
    if (guider_ui.screen_relatorio_final.screen == NULL) {
        return;
    }
    ui_flow_fill_final_report_container(guider_ui.screen_relatorio_final.container_dados_cliente,
                                        customer_summary);
    ui_flow_fill_final_report_container(guider_ui.screen_relatorio_final.container_dados_ciclo,
                                        cycle_summary);
    lv_obj_add_event_cb(guider_ui.screen_relatorio_final.button_menu_principal,
                        ui_flow_final_report_menu_cb, LV_EVENT_CLICKED, NULL);
    if (s_ui_flow.final_report_from_list) {
        lv_label_set_text(guider_ui.screen_relatorio_final.button_menu_principal_label_bt_menu_principal,
                          "Voltar");
    }
    lv_screen_load_anim(guider_ui.screen_relatorio_final.screen,
                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/** @brief Fecha o teclado usado no campo de busca de relatórios. */
static void ui_flow_close_report_search_keyboard(void)
{
    if (s_ui_flow.report_search_keyboard != NULL) {
        lv_obj_delete(s_ui_flow.report_search_keyboard);
        s_ui_flow.report_search_keyboard = NULL;
        s_ui_flow.report_search_textarea = NULL;
    }
}

/**
 * @brief Confirma o texto da busca visual de relatórios.
 *
 * O filtro será acrescentado posteriormente; nesta etapa o texto apenas é exibido no campo Buscar.
 *
 * @param[in] event Evento de confirmação do teclado.
 */
static void ui_flow_report_search_ready_cb(lv_event_t *event)
{
    (void)event;
    if (s_ui_flow.report_search_textarea != NULL) {
        strncpy(s_ui_flow.report_search_text, lv_textarea_get_text(s_ui_flow.report_search_textarea),
                sizeof(s_ui_flow.report_search_text) - 1U);
        s_ui_flow.report_search_text[sizeof(s_ui_flow.report_search_text) - 1U] = '\0';
    }
    if (s_ui_flow.report_search_label != NULL) {
        lv_label_set_text(s_ui_flow.report_search_label,
                          s_ui_flow.report_search_text[0] == '\0' ? "Buscar" : s_ui_flow.report_search_text);
    }
    ui_flow_close_report_search_keyboard();
    if (s_ui_flow.report_search_label == NULL) {
        return;
    }
    bool has_match = false;
    ui_flow_populate_reports_list(s_ui_flow.report_search_text, &has_match);
    if (!has_match) {
        ui_flow_show_search_empty_popup();
    }
}

/**
 * @brief Cancela a busca visual sem alterar o texto exibido.
 *
 * @param[in] event Evento de cancelamento do teclado.
 */
static void ui_flow_report_search_cancel_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_report_search_keyboard();
}

/**
 * @brief Abre o teclado para escrever no campo Buscar da lista de relatórios.
 *
 * @param[in] event Evento de toque da imagem de busca.
 */
static void ui_flow_show_report_search_keyboard(lv_event_t *event)
{
    (void)event;
    ui_flow_close_report_search_keyboard();
    s_ui_flow.report_search_keyboard = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.report_search_keyboard, 800, 350);
    lv_obj_set_pos(s_ui_flow.report_search_keyboard, 0, 130);
    lv_obj_set_style_bg_color(s_ui_flow.report_search_keyboard, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.report_search_keyboard, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.report_search_keyboard, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(s_ui_flow.report_search_keyboard);
    lv_label_set_text(title, "Buscar Relatorio");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_pos(title, 16, 5);
    s_ui_flow.report_search_textarea = lv_textarea_create(s_ui_flow.report_search_keyboard);
    lv_obj_set_size(s_ui_flow.report_search_textarea, 500, 42);
    lv_obj_set_pos(s_ui_flow.report_search_textarea, 16, 27);
    lv_textarea_set_one_line(s_ui_flow.report_search_textarea, true);
    lv_textarea_set_text(s_ui_flow.report_search_textarea, s_ui_flow.report_search_text);
    lv_obj_t *keyboard = lv_keyboard_create(s_ui_flow.report_search_keyboard);
    lv_obj_set_size(keyboard, 760, 220);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(keyboard, s_ui_flow.report_search_textarea);
    lv_obj_add_event_cb(keyboard, ui_flow_report_search_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, ui_flow_report_search_cancel_cb, LV_EVENT_CANCEL, NULL);
}

/**
 * @brief Abre o relatório selecionado na lista persistida.
 *
 * @param[in] event Evento de toque de um card de relatório.
 */
static void ui_flow_report_card_click_cb(lv_event_t *event)
{
    const uint32_t recent_index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    if (report_storage_get_recent(recent_index, &s_ui_flow.selected_report) == ESP_OK) {
        s_ui_flow.selected_report_valid = true;
        s_ui_flow.final_report_from_list = true;
        ui_flow_show_final_report();
    }
}

/**
 * @brief Cria um card compacto para um relatório armazenado.
 *
 * @param[in] parent Container rolável que receberá o card.
 * @param[in] record Relatório a resumir no card.
 * @param[in] recent_index Índice usado para recuperar o relatório quando o card for selecionado.
 */
static void ui_flow_add_report_card(lv_obj_t *parent, const report_storage_record_t *record,
                                    uint32_t recent_index)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_size(card, lv_pct(100), 82);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x151a1c), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_t *date = lv_label_create(card);
    lv_label_set_text_fmt(date, "%s - %s", record->date, record->start_time);
    lv_obj_set_style_text_color(date, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(date, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_set_pos(date, 12, 7);
    lv_obj_t *description = lv_label_create(card);
    lv_obj_set_width(description, 540);
    lv_label_set_long_mode(description, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(description, "%s - %s", record->customer_name, record->vehicle);
    lv_obj_set_style_text_color(description, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(description, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_set_pos(description, 12, 43);
    lv_obj_t *arrow = lv_label_create(card);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_color(arrow, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(arrow, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_add_event_cb(card, ui_flow_report_card_click_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)recent_index);
}

/**
 * @brief Verifica se um texto está contido em outro sem diferenciar maiúsculas e minúsculas ASCII.
 *
 * @param[in] text Texto no qual a busca será realizada.
 * @param[in] filter Termo de busca informado pelo usuário.
 * @return @c true quando o termo ocorrer no texto ou estiver vazio.
 */
static bool ui_flow_report_name_matches(const char *text, const char *filter)
{
    if (filter == NULL || filter[0] == '\0') {
        return true;
    }
    for (; *text != '\0'; text++) {
        const char *text_cursor = text;
        const char *filter_cursor = filter;
        while (*text_cursor != '\0' && *filter_cursor != '\0' &&
               tolower((unsigned char)*text_cursor) == tolower((unsigned char)*filter_cursor)) {
            text_cursor++;
            filter_cursor++;
        }
        if (*filter_cursor == '\0') {
            return true;
        }
    }
    return false;
}

/**
 * @brief Recria a lista com todos os relatórios ou apenas os nomes que correspondem ao filtro.
 *
 * @param[in] filter Texto a procurar nos nomes dos clientes.
 * @param[out] out_has_match Recebe se pelo menos um card foi criado, opcional.
 */
static void ui_flow_populate_reports_list(const char *filter, bool *out_has_match)
{
    if (out_has_match != NULL) {
        *out_has_match = false;
    }
    lv_obj_t *list = guider_ui.screen_relatorio.container_relatorios_infos;
    if (list == NULL) {
        return;
    }
    lv_obj_clean(list);
    uint32_t count = 0;
    if (report_storage_get_count(&count) != ESP_OK) {
        return;
    }
    for (uint32_t index = 0; index < count; index++) {
        report_storage_record_t record = {0};
        if (report_storage_get_recent(index, &record) == ESP_OK &&
            ui_flow_report_name_matches(record.customer_name, filter)) {
            ui_flow_add_report_card(list, &record, index);
            if (out_has_match != NULL) {
                *out_has_match = true;
            }
        }
    }
}

/**
 * @brief Fecha o aviso de lista vazia e retorna ao menu principal.
 *
 * @param[in] event Evento de toque do botão OK.
 */
static void ui_flow_no_reports_ok_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_report_popup();
    ui_flow_show_main_menu();
}

/** @brief Mostra aviso quando não há relatórios persistidos. */
static void ui_flow_show_no_reports_popup(void)
{
    ui_flow_close_report_popup();
    s_ui_flow.report_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.report_popup, 360, 150);
    lv_obj_center(s_ui_flow.report_popup);
    lv_obj_set_style_bg_color(s_ui_flow.report_popup, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.report_popup, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.report_popup, 2, LV_PART_MAIN);
    lv_obj_t *message = lv_label_create(s_ui_flow.report_popup);
    lv_label_set_text(message, "Nenhum relatorio salvo.");
    lv_obj_set_style_text_color(message, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *ok = lv_button_create(s_ui_flow.report_popup);
    lv_obj_set_size(ok, 100, 36);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(ok, ui_flow_no_reports_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(ok);
    lv_label_set_text(label, "OK");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(label);
}

/**
 * @brief Fecha o aviso de busca sem resultados e mantém a tela de relatórios aberta.
 *
 * @param[in] event Evento de toque do botão OK.
 */
static void ui_flow_search_empty_ok_cb(lv_event_t *event)
{
    (void)event;
    ui_flow_close_report_popup();
}

/** @brief Mostra aviso quando nenhum nome corresponde ao texto pesquisado. */
static void ui_flow_show_search_empty_popup(void)
{
    ui_flow_close_report_popup();
    s_ui_flow.report_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ui_flow.report_popup, 380, 150);
    lv_obj_center(s_ui_flow.report_popup);
    lv_obj_set_style_bg_color(s_ui_flow.report_popup, lv_color_hex(0x101416), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui_flow.report_popup, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui_flow.report_popup, 2, LV_PART_MAIN);
    lv_obj_t *message = lv_label_create(s_ui_flow.report_popup);
    lv_label_set_text(message, "A busca nao encontrou nada.");
    lv_obj_set_style_text_color(message, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *ok = lv_button_create(s_ui_flow.report_popup);
    lv_obj_set_size(ok, 100, 36);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(ok, ui_flow_search_empty_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(ok);
    lv_label_set_text(label, "OK");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_center(label);
}

/** @brief Cria a lista rolável de relatórios salvos, ordenada do mais recente ao mais antigo. */
static void ui_flow_show_reports(void)
{
    uint32_t count = 0;
    if (report_storage_get_count(&count) != ESP_OK || count == 0) {
        ui_flow_show_no_reports_popup();
        return;
    }
    if (s_ui_flow.menu_clock_timer != NULL) {
        lv_timer_delete(s_ui_flow.menu_clock_timer);
        s_ui_flow.menu_clock_timer = NULL;
    }
    memset(&guider_ui.screen_relatorio, 0, sizeof(guider_ui.screen_relatorio));
    setup_screen_relatorio(&guider_ui);
    if (guider_ui.screen_relatorio.screen == NULL) {
        return;
    }
    lv_obj_add_event_cb(guider_ui.screen_relatorio.button_voltar,
                        ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(guider_ui.screen_relatorio.button_home,
                        ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *search = guider_ui.screen_relatorio.container_buscar;
    memset(s_ui_flow.report_search_text, 0, sizeof(s_ui_flow.report_search_text));
    lv_obj_set_scroll_dir(search, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(search, LV_SCROLLBAR_MODE_OFF);
    s_ui_flow.report_search_label = lv_label_create(search);
    lv_label_set_text(s_ui_flow.report_search_label, "Buscar");
    lv_obj_set_style_text_color(s_ui_flow.report_search_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(s_ui_flow.report_search_label, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_add_flag(guider_ui.screen_relatorio.image_buscar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(guider_ui.screen_relatorio.image_buscar, ui_flow_show_report_search_keyboard,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *list = guider_ui.screen_relatorio.container_relatorios_infos;
    lv_obj_clean(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_left(list, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_right(list, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_top(list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 7, LV_PART_MAIN);
    ui_flow_populate_reports_list("", NULL);
    lv_screen_load_anim(guider_ui.screen_relatorio.screen,
                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * @brief Adiciona uma linha de estado ao painel de diagnóstico.
 *
 * @param[in] parent Container que receberá a linha.
 * @param[in] name Nome do parâmetro de diagnóstico.
 * @param[in] value Valor demonstrativo do parâmetro.
 */
static void ui_flow_add_diagnostic_row(lv_obj_t *parent, const char *name, const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 38);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x151a1c), LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2f3539), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 12, LV_PART_MAIN);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);
    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(name_label, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(value_label, &lv_font_montserratMedium_20, LV_PART_MAIN);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

/** @brief Cria e apresenta a tela de diagnóstico do equipamento. */
static void ui_flow_show_diagnostic(void)
{
    if (s_ui_flow.menu_clock_timer != NULL) {
        lv_timer_delete(s_ui_flow.menu_clock_timer);
        s_ui_flow.menu_clock_timer = NULL;
    }
    memset(&guider_ui.screen_diagnostico, 0, sizeof(guider_ui.screen_diagnostico));
    setup_screen_diagnostico(&guider_ui);
    if (guider_ui.screen_diagnostico.screen == NULL) {
        return;
    }
    lv_obj_add_event_cb(guider_ui.screen_diagnostico.button_voltar,
                        ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(guider_ui.screen_diagnostico.button_home,
                        ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *list = guider_ui.screen_diagnostico.container_list;
    lv_obj_clean(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_left(list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);
    ui_flow_add_diagnostic_row(list, "Tensao do Sistema", "12.5 V");
    ui_flow_add_diagnostic_row(list, "Pressao Atual", "150 bar");
    ui_flow_add_diagnostic_row(list, "Temperatura", "45 C");
    ui_flow_add_diagnostic_row(list, "Bomba", "Ligada");
    ui_flow_add_diagnostic_row(list, "Dreno", "Fechado");
    ui_flow_add_diagnostic_row(list, "Ultrassom", "OK");
    ui_flow_add_diagnostic_row(list, "Eletrovalvula", "OK");
    lv_screen_load_anim(guider_ui.screen_diagnostico.screen,
                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/** @brief Cria, conecta e carrega a tela de seleção de testes automáticos. */
static void ui_flow_show_automatic_tests(void)
{
    memset(&guider_ui.screen_testes_automaticos, 0, sizeof(guider_ui.screen_testes_automaticos));
    setup_screen_testes_automaticos(&guider_ui);
    if (guider_ui.screen_testes_automaticos.screen == NULL) {
        return;
    }
    if (guider_ui.screen_testes_automaticos.button_voltar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_testes_automaticos.button_voltar,
                            ui_flow_bicos_step_two_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_testes_automaticos.button_home != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_testes_automaticos.button_home,
                            ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *test_buttons[] = {
        guider_ui.screen_testes_automaticos.button_teste_leque,
        guider_ui.screen_testes_automaticos.button_teste_equalizacao_vazao,
        guider_ui.screen_testes_automaticos.button_equa_vas_temp_maior,
        guider_ui.screen_testes_automaticos.button_estanqueidade,
        guider_ui.screen_testes_automaticos.button_alvo_do_teste_em_rotacoes,
        guider_ui.screen_testes_automaticos.button_leque_vazao_equa,
        guider_ui.screen_testes_automaticos.button_auto
    };
    static const ui_flow_automatic_test_t tests[] = {
        {.description = "Teste Leque", .operation_mode = 2},
        {.description = "Equalizacao de Vazao", .operation_mode = 3},
        {.description = "Equalizacao Vazao/Temperatura", .operation_mode = 4},
        {.description = "Teste de Estanqueidade", .operation_mode = 5},
        {.description = "Teste em Rotacoes", .operation_mode = 6},
        {.description = "Leque/Vazao/Equalizacao", .operation_mode = 7},
        {.description = "Automatico", .operation_mode = 8},
    };
    for (uint32_t index = 0; index < sizeof(test_buttons) / sizeof(test_buttons[0]); index++) {
        if (test_buttons[index] != NULL) {
            lv_obj_add_event_cb(test_buttons[index], ui_flow_automatic_test_start_cb,
                                LV_EVENT_CLICKED, (void *)&tests[index]);
        }
    }
    if (guider_ui.screen_testes_automaticos.button_mais != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_testes_automaticos.button_mais,
                            ui_flow_automatic_tests_more_button_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_screen_load_anim(guider_ui.screen_testes_automaticos.screen,
                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/** @brief Cria, conecta e carrega a segunda página de testes automáticos. */
static void ui_flow_show_automatic_tests_second_page(void)
{
    memset(&guider_ui.screen_testes_automaticos_2, 0, sizeof(guider_ui.screen_testes_automaticos_2));
    setup_screen_testes_automaticos_2(&guider_ui);
    if (guider_ui.screen_testes_automaticos_2.screen == NULL) {
        return;
    }
    if (guider_ui.screen_testes_automaticos_2.button_voltar != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_testes_automaticos_2.button_voltar,
                            ui_flow_automatic_tests_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_testes_automaticos_2.button_home != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_testes_automaticos_2.button_home,
                            ui_flow_main_menu_button_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *test_buttons[] = {
        guider_ui.screen_testes_automaticos_2.button_leque_vazao_equa,
        guider_ui.screen_testes_automaticos_2.button_teste_circulacao
    };
    static const ui_flow_automatic_test_t tests[] = {
        {.description = "Leque/Vazao/Equalizacao", .operation_mode = 9},
        {.description = "Teste de Circulacao", .operation_mode = 10},
    };
    for (uint32_t index = 0; index < sizeof(test_buttons) / sizeof(test_buttons[0]); index++) {
        if (test_buttons[index] != NULL) {
            lv_obj_add_event_cb(test_buttons[index], ui_flow_automatic_test_start_cb,
                                LV_EVENT_CLICKED, (void *)&tests[index]);
        }
    }
    lv_screen_load_anim(guider_ui.screen_testes_automaticos_2.screen,
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
    if (guider_ui.screen_configuracoes.container_configuracoes_button_parametros_gerais != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.container_configuracoes_button_parametros_gerais,
                            ui_flow_general_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_configuracoes.container_configuracoes_button_sobre != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.container_configuracoes_button_sobre,
                            ui_flow_about_button_cb, LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.screen_configuracoes.container_configuracoes_button_manutencao != NULL) {
        lv_obj_add_event_cb(guider_ui.screen_configuracoes.container_configuracoes_button_manutencao,
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
