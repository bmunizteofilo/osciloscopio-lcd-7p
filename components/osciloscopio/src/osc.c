#include "osc.h"
#include "acquisition_history.h"
#include "gt911_touch.h"
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "draw/lv_draw.h"

#define OSC_SCREEN_WIDTH WT32S3_LCD_H_RES
#define OSC_SCREEN_HEIGHT WT32S3_LCD_V_RES
#define OSC_MENU_HEIGHT 56
#define OSC_MENU_LABEL_HEIGHT 16
#define OSC_MENU_DROPDOWN_HEIGHT 36
#define OSC_MENU_ITEM_WIDTH 118
#define OSC_WAVEFORM_Y 58
#define OSC_WAVEFORM_HEIGHT 368
#define OSC_WAVEFORM_BORDER 2
#define OSC_CHANNEL_BUTTON_PANEL_WIDTH 100
#define OSC_INFORMATION_PANEL_WIDTH 100
#define OSC_CHANNEL_BUTTON_GAP 4
#define OSC_CHANNEL_BUTTON_Y_OFFSET 3
#define OSC_CHANNEL_BUTTON_WIDTH 92
#define OSC_CHANNEL_BUTTON_HEIGHT 86
#define OSC_INFORMATION_PANEL_CONTENT_WIDTH (OSC_CHANNEL_BUTTON_WIDTH + 8)
#define OSC_WAVEFORM_X OSC_CHANNEL_BUTTON_PANEL_WIDTH
#define OSC_WAVEFORM_WIDTH 590
#define OSC_INFORMATION_PANEL_X (OSC_WAVEFORM_X + OSC_WAVEFORM_WIDTH)
#define OSC_WAVEFORM_VERTICAL_DIVISION_WIDTH 59
#define OSC_WAVEFORM_CENTER_X (OSC_WAVEFORM_X + (5 * OSC_WAVEFORM_VERTICAL_DIVISION_WIDTH))
#define OSC_WAVEFORM_INNER_X (OSC_WAVEFORM_X + OSC_WAVEFORM_BORDER)
#define OSC_WAVEFORM_INNER_Y (OSC_WAVEFORM_Y + OSC_WAVEFORM_BORDER)
#define OSC_WAVEFORM_INNER_WIDTH (OSC_WAVEFORM_WIDTH - (OSC_WAVEFORM_BORDER * 2))
#define OSC_WAVEFORM_INNER_HEIGHT (OSC_WAVEFORM_HEIGHT - (OSC_WAVEFORM_BORDER * 2))
#define OSC_MEASUREMENTS_Y 430
#define OSC_CHANNEL_ROW_HEIGHT 25
#define OSC_CHANNEL_INFO_WIDTH (OSC_SCREEN_WIDTH / 2)
#define OSC_MEASUREMENTS_VISIBLE_ROWS 2
#define OSC_MEASUREMENTS_HEIGHT (OSC_CHANNEL_ROW_HEIGHT * OSC_MEASUREMENTS_VISIBLE_ROWS)
#define OSC_ACTION_PANEL_X OSC_CHANNEL_INFO_WIDTH
#define OSC_ACTION_PANEL_WIDTH (OSC_SCREEN_WIDTH - OSC_ACTION_PANEL_X)
#define OSC_ACTION_BUTTON_GAP 8
#define OSC_ACTION_BUTTON_WIDTH 125
#define OSC_ACTION_BUTTON_HEIGHT 42
#define OSC_ACTION_BUTTON_Y (OSC_MEASUREMENTS_Y + 4)
#define OSC_CHART_POINT_COUNT OSC_WAVEFORM_INNER_WIDTH
#define OSC_LIVE_ENVELOPE_VALUE_COUNT (4U * OSC_WAVEFORM_INNER_WIDTH)
#define OSC_HISTORY_BYTES_PER_CHANNEL (500U * 1024U)
#define OSC_HISTORY_SAMPLE_COUNT (OSC_HISTORY_BYTES_PER_CHANNEL / sizeof(uint16_t))
#define OSC_DEFAULT_INPUT_SAMPLE_RATE_HZ 2000U
#define OSC_ADC_CENTER 1024
#define OSC_ADC_COUNTS_PER_DIV 256
#define OSC_DEFAULT_TIME_BASE_US_PER_DIV 100000
#define OSC_DEFAULT_VOLTAGE_BASE_MV_PER_DIV 50
#define OSC_TIME_DIV_COUNT 10
#define OSC_VOLTAGE_DIV_COUNT 8
#define OSC_BACKLIGHT_DEFAULT_PERCENT 80
#define OSC_BACKLIGHT_MIN_PERCENT 10
#define OSC_BACKLIGHT_MAX_PERCENT 100
#define OSC_BACKLIGHT_SLIDER_WIDTH 36
#define OSC_BACKLIGHT_SLIDER_HEIGHT ((OSC_WAVEFORM_INNER_HEIGHT * 60) / 100)
#define OSC_TRIGGER_HIDE_MS 5000
#define OSC_TRIGGER_MARKER_WIDTH 5
#define OSC_TRIGGER_SCAN_MAX_SAMPLES 2048U
/** @brief Intervalo mínimo entre atualizações visuais da waveform ao vivo. */
#define OSC_WAVEFORM_RENDER_INTERVAL_US (1000000ULL / 30ULL)

typedef enum {
    OSC_CURSOR_MODE_OFF = 0,
    OSC_CURSOR_MODE_TIME,
    OSC_CURSOR_MODE_VOLTAGE,
} osc_cursor_mode_t;

typedef enum {
    OSC_THEME_DARK = 0,
    OSC_THEME_LIGHT,
} osc_theme_t;

static const char *TAG = "osc";

static const uint32_t OSC_CHANNEL_COLORS[] = {
    0x00c853, /* CH1 verde */
    0xffd600, /* CH2 amarelo */
    0xff1744, /* CH3 vermelho */
    0x2979ff, /* CH4 azul */
};

static const int32_t OSC_MEASUREMENT_X[] = {
    8,
    130,
    250,
    385,
    530,
};

static const uint32_t OSC_TIME_BASE_US_PER_DIV[] = {
    1000,
    2000,
    5000,
    10000,
    20000,
    50000,
    100000,
    250000,
    500000,
    1000000,
};

static const uint32_t OSC_VOLTAGE_BASE_MV_PER_DIV[] = {
    50,
    100,
    200,
    500,
    1000,
    2000,
    5000,
    10000,
};

typedef struct {
    wt32s3_lcd_handle_t lcd;
    gt911_touch_handle_t touch;
    lv_display_t *display;
    lv_indev_t *input;
    lv_obj_t *screen;
    lv_obj_t *waveform_renderer;
    lv_obj_t *buffer_label;
    lv_obj_t *channel_buttons[4];
    lv_obj_t *information_titles[4];
    lv_obj_t *information_values[4];
    lv_obj_t *cursor_lines[2];
    lv_obj_t *cursor_label;
    lv_obj_t *trigger_line;
    lv_obj_t *trigger_marker;
    lv_timer_t *trigger_hide_timer;
    lv_timer_t *measurement_timer;
    lv_obj_t *backlight_slider;
    lv_obj_t *backlight_label;
    lv_obj_t *measurement_labels[4][5];
    lv_obj_t *pause_button;
    lv_obj_t *stop_cycle_button;
    lv_obj_t *run_pause_dropdown;
    lv_obj_t *pause_button_label;
    lv_obj_t *stop_cycle_button_label;
    lv_timer_t *stop_cycle_blink_timer;
    bool channel_visible[4];
    osc_cursor_mode_t cursor_mode;
    osc_theme_t theme;
    bool paused;
    bool stop_cycle_requested;
    bool stop_cycle_blink_on;
    bool trigger_enabled;
    bool trigger_rising;
    bool trigger_single_captured;
    bool trigger_single_capture_pending;
    uint8_t trigger_mode;
    uint8_t trigger_channel;
    uint32_t trigger_single_first_frame;
    uint32_t trigger_single_pending_first_frame;
    uint32_t trigger_single_pending_sample_count;
    uint16_t *trigger_single_samples[4];
    uint32_t trigger_single_sample_count;
    uint8_t backlight_percent;
    uint32_t time_base_us_per_div;
    uint32_t voltage_base_mv_per_div;
    uint32_t input_sample_rate_hz;
    uint16_t *live_envelope_min;
    uint16_t *live_envelope_max;
    uint16_t *live_envelope_first;
    uint16_t *live_envelope_last;
    uint32_t live_envelope_window_samples;
    uint32_t live_envelope_progress;
    uint16_t live_envelope_head;
    uint16_t live_envelope_column_count;
    bool live_envelope_column_open;
    uint32_t waveform_sample_count;
    uint32_t waveform_sample_head;
    uint32_t waveform_total_frames;
    uint32_t waveform_generation;
    uint32_t trigger_scan_total_frames;
    uint32_t trigger_latest_event_frame;
    uint32_t trigger_candidate_event_frame;
    uint32_t trigger_normal_first_frame;
    bool trigger_scan_initialized;
    bool trigger_latest_event_valid;
    bool trigger_candidate_event_valid;
    bool trigger_normal_hold_valid;
    bool pause_view_valid;
    uint32_t history_view_offset;
    uint32_t pause_first_frame;
    int32_t history_drag_last_x;
    bool history_navigation_started;
    uint32_t last_plot_first_sample;
    uint32_t last_plot_sample_count;
    bool last_plot_triggered;
    uint64_t last_live_render_request_us;
    SemaphoreHandle_t lock;
    TaskHandle_t task;
} osc_context_t;

static osc_context_t s_lvgl = {0};
static uint16_t s_waveform_y_lookup[(OSC_ADC_CENTER * 2) + 1];

/**
 * @brief Bloqueia o acesso as APIs do LVGL.
 */
static void osc_update_cursor_label(void);
static void osc_reset_cursor_positions(void);
static void osc_waveform_reset(void);
static void osc_update_buffer_label(void);
static void osc_live_envelope_reset(void);
static void osc_request_live_waveform_render(void);
static void osc_trigger_edge_event_cb(lv_event_t *event);
static void osc_trigger_channel_event_cb(lv_event_t *event);
static void osc_clear_measurements(uint8_t channel);
static void osc_update_action_buttons(void);
static void osc_trigger_reset_search(void);
static void osc_trigger_reset_detector(void);
static void osc_trigger_scan_new_samples(void);
static void osc_trigger_capture_single(void);
static void osc_set_paused(bool paused);
static void osc_trigger_clear_single_capture(void);

/** @brief Callback registrado pelo fluxo de telas para retornar ao menu principal. */
static osc_menu_callback_t s_menu_callback;
/** @brief Callback registrado pelo fluxo de telas para encerrar o ciclo atual. */
static osc_cycle_finished_callback_t s_cycle_finished_callback;
/** @brief Callback chamado ao trocar o perfil ADC da base de tempo. */
static osc_profile_changed_callback_t s_profile_changed_callback;
/** @brief Callback chamado para pausar ou retomar o ciclo na Power Control. */
static osc_cycle_pause_callback_t s_cycle_pause_callback;

static uint32_t osc_theme_waveform_bg(void)
{
    return s_lvgl.theme == OSC_THEME_LIGHT ? 0xffffff : 0x000000;
}

static uint32_t osc_theme_grid_dot(void)
{
    return s_lvgl.theme == OSC_THEME_LIGHT ? 0x404040 : 0x606060;
}

static uint32_t osc_theme_grid_center(void)
{
    return s_lvgl.theme == OSC_THEME_LIGHT ? 0x000000 : 0xffffff;
}

static uint32_t osc_theme_overlay_text(void)
{
    return s_lvgl.theme == OSC_THEME_LIGHT ? 0x000000 : 0xffffff;
}

static void osc_apply_theme(void)
{
    if (s_lvgl.waveform_renderer != NULL) {
        lv_obj_invalidate(s_lvgl.waveform_renderer);
    }

    for (uint8_t i = 0; i < 2; i++) {
        if (s_lvgl.cursor_lines[i] != NULL) {
            lv_obj_set_style_bg_color(s_lvgl.cursor_lines[i], lv_color_hex(osc_theme_overlay_text()), LV_PART_MAIN);
        }
    }

    if (s_lvgl.cursor_label != NULL) {
        lv_obj_set_style_text_color(s_lvgl.cursor_label, lv_color_hex(osc_theme_overlay_text()), LV_PART_MAIN);
    }
    if (s_lvgl.backlight_label != NULL) {
        lv_obj_set_style_text_color(s_lvgl.backlight_label, lv_color_hex(osc_theme_overlay_text()), LV_PART_MAIN);
    }
    if (s_lvgl.buffer_label != NULL) {
        lv_obj_set_style_text_color(s_lvgl.buffer_label, lv_color_hex(osc_theme_overlay_text()), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_lvgl.buffer_label, lv_color_hex(osc_theme_waveform_bg()), LV_PART_MAIN);
    }
}

/**
 * @brief Atualiza a base de tempo usada para apresentar as amostras recebidas.
 *
 * @param[in] event Evento LVGL do dropdown de base de tempo.
 */
static void osc_time_base_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected < (sizeof(OSC_TIME_BASE_US_PER_DIV) / sizeof(OSC_TIME_BASE_US_PER_DIV[0]))) {
        s_lvgl.time_base_us_per_div = OSC_TIME_BASE_US_PER_DIV[selected];
        osc_live_envelope_reset();
        const uint8_t new_profile = osc_get_acquisition_profile();
        if (s_profile_changed_callback != NULL) {
            s_profile_changed_callback(new_profile);
        }
        if (s_lvgl.waveform_renderer != NULL) {
            lv_obj_invalidate(s_lvgl.waveform_renderer);
        }
        osc_update_cursor_label();
    }
}

/**
 * @brief Atualiza a base de tensao usada pelos cursores horizontais.
 *
 * @param[in] event Evento LVGL do dropdown de base de tensao.
 */
static void osc_voltage_base_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected < (sizeof(OSC_VOLTAGE_BASE_MV_PER_DIV) / sizeof(OSC_VOLTAGE_BASE_MV_PER_DIV[0]))) {
        s_lvgl.voltage_base_mv_per_div = OSC_VOLTAGE_BASE_MV_PER_DIV[selected];
        osc_update_cursor_label();
    }
}

/**
 * @brief Atualiza o modo dos cursores.
 *
 * @param[in] event Evento LVGL do dropdown de cursores.
 */
static void osc_cursor_mode_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected <= OSC_CURSOR_MODE_VOLTAGE) {
        s_lvgl.cursor_mode = (osc_cursor_mode_t)selected;
        osc_reset_cursor_positions();
        osc_update_cursor_label();
    }
}

/**
 * @brief Atualiza o estado de execucao do sinal.
 *
 * @param[in] event Evento LVGL do dropdown Run/Pause.
 */
static void osc_run_pause_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    osc_set_paused(selected == 1U);
}

/**
 * @brief Esconde visualmente a linha de trigger mantendo a area de toque ativa.
 *
 * @param[in] timer Timer LVGL do auto-hide.
 */
static void osc_trigger_hide_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_lvgl.trigger_line != NULL && s_lvgl.trigger_enabled) {
        lv_obj_set_style_bg_opa(s_lvgl.trigger_line, LV_OPA_TRANSP, LV_PART_MAIN);
        if (s_lvgl.trigger_marker != NULL) {
            lv_obj_set_y(s_lvgl.trigger_marker, lv_obj_get_y(s_lvgl.trigger_line));
            lv_obj_clear_flag(s_lvgl.trigger_marker, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_lvgl.trigger_marker);
        }
    }
    if (s_lvgl.trigger_hide_timer != NULL) {
        lv_timer_pause(s_lvgl.trigger_hide_timer);
    }
}

/**
 * @brief Mostra a linha de trigger e reinicia o timer de auto-hide.
 */
static void osc_show_trigger_line(void)
{
    if (s_lvgl.trigger_line == NULL || !s_lvgl.trigger_enabled) {
        return;
    }

    lv_obj_set_style_bg_opa(s_lvgl.trigger_line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_move_foreground(s_lvgl.trigger_line);
    if (s_lvgl.trigger_marker != NULL) {
        lv_obj_add_flag(s_lvgl.trigger_marker, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_lvgl.trigger_hide_timer != NULL) {
        lv_timer_reset(s_lvgl.trigger_hide_timer);
        lv_timer_resume(s_lvgl.trigger_hide_timer);
    }
}

/**
 * @brief Move a linha de trigger pelo toque.
 *
 * @param[in] event Evento LVGL da linha de trigger.
 */
static void osc_trigger_line_event_cb(lv_event_t *event)
{
    (void)event;
    if (!s_lvgl.trigger_enabled || s_lvgl.trigger_line == NULL) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_point_t point = {0};
    lv_indev_get_point(indev, &point);

    int32_t y = point.y;
    if (y < OSC_WAVEFORM_INNER_Y) {
        y = OSC_WAVEFORM_INNER_Y;
    } else if (y > OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT - 2) {
        y = OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT - 2;
    }
    lv_obj_set_y(s_lvgl.trigger_line, y);
    /* Mantem a ultima captura normal visivel enquanto procura a nova borda. */
    osc_trigger_reset_search();
    osc_show_trigger_line();
    lv_obj_invalidate(s_lvgl.waveform_renderer);
}

/**
 * @brief Atualiza a habilitacao visual do nivel de trigger.
 *
 * @param[in] event Evento LVGL do dropdown Trigger.
 */
static void osc_trigger_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    s_lvgl.trigger_mode = (uint8_t)lv_dropdown_get_selected(dropdown);
    s_lvgl.trigger_enabled = s_lvgl.trigger_mode != 0;
    osc_trigger_reset_detector();
    osc_live_envelope_reset();

    if (s_lvgl.trigger_line == NULL) {
        return;
    }

    if (s_lvgl.trigger_enabled) {
        osc_show_trigger_line();
    } else {
        lv_obj_set_style_bg_opa(s_lvgl.trigger_line, LV_OPA_TRANSP, LV_PART_MAIN);
        if (s_lvgl.trigger_marker != NULL) {
            lv_obj_add_flag(s_lvgl.trigger_marker, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_lvgl.trigger_hide_timer != NULL) {
            lv_timer_pause(s_lvgl.trigger_hide_timer);
        }
    }
    osc_update_buffer_label();
    if (s_lvgl.waveform_renderer != NULL) {
        lv_obj_invalidate(s_lvgl.waveform_renderer);
    }
}

/**
 * @brief Atualiza o label e o PWM do backlight.
 */
static void osc_update_backlight_value(void)
{
    if (s_lvgl.backlight_label != NULL) {
        lv_label_set_text_fmt(s_lvgl.backlight_label, "%u%%", (unsigned)s_lvgl.backlight_percent);
    }

    if (s_lvgl.lcd != NULL) {
        esp_err_t err = wt32s3_lcd_set_backlight(s_lvgl.lcd, s_lvgl.backlight_percent);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "falha ao ajustar brilho: %s", esp_err_to_name(err));
        }
    }
}

/**
 * @brief Processa alteracoes no slider de brilho.
 *
 * @param[in] event Evento LVGL do slider.
 */
static void osc_backlight_slider_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    s_lvgl.backlight_percent = (uint8_t)lv_slider_get_value(slider);
    osc_update_backlight_value();
}

/**
 * @brief Mostra ou esconde o controle de brilho.
 *
 * @param[in] event Evento LVGL do dropdown Brilho.
 */
static void osc_backlight_dropdown_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const bool show = lv_dropdown_get_selected(dropdown) == 0;

    if (s_lvgl.backlight_slider == NULL || s_lvgl.backlight_label == NULL) {
        return;
    }

    if (show) {
        lv_obj_clear_flag(s_lvgl.backlight_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_lvgl.backlight_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_lvgl.backlight_slider);
        lv_obj_move_foreground(s_lvgl.backlight_label);
    } else {
        lv_obj_add_flag(s_lvgl.backlight_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lvgl.backlight_label, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Alterna o tema visual da waveform.
 *
 * @param[in] event Evento LVGL do dropdown Tema.
 */
static void osc_theme_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    s_lvgl.theme = selected == 1 ? OSC_THEME_LIGHT : OSC_THEME_DARK;
    osc_apply_theme();
}

/**
 * @brief Cria um item do menu superior com label e dropdown.
 *
 * @param[in] parent Objeto pai rolavel.
 * @param[in] title Titulo exibido acima do dropdown.
 * @param[in] options Opcoes do dropdown separadas por '\n'.
 * @param[in] selected Opcao selecionada inicialmente.
 * @param[in] color Cor de fundo do controle.
 * @param[in] event_cb Callback opcional de alteracao.
 */
static lv_obj_t *osc_create_menu_item(lv_obj_t *parent, const char *title, const char *options, uint16_t selected, uint32_t color, lv_event_cb_t event_cb)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, OSC_MENU_ITEM_WIDTH, OSC_MENU_HEIGHT);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(item);
    lv_label_set_text(label, title);
    lv_obj_set_size(label, OSC_MENU_ITEM_WIDTH, OSC_MENU_LABEL_HEIGHT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *dropdown = lv_dropdown_create(item);
    lv_obj_set_size(dropdown, OSC_MENU_ITEM_WIDTH, OSC_MENU_DROPDOWN_HEIGHT);
    lv_obj_set_pos(dropdown, 0, OSC_MENU_LABEL_HEIGHT + 1);
    lv_dropdown_set_options(dropdown, options);
    lv_dropdown_set_selected(dropdown, selected);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown, 5, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_pad_top(dropdown, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(dropdown, 0, LV_PART_MAIN);
    if (event_cb != NULL) {
        lv_obj_add_event_cb(dropdown, event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    return dropdown;
}

/**
 * @brief Cria o menu superior horizontal e rolavel.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_top_menu(lv_obj_t *parent)
{
    lv_obj_t *menu = lv_obj_create(parent);
    lv_obj_remove_style_all(menu);
    lv_obj_set_size(menu, OSC_SCREEN_WIDTH, OSC_MENU_HEIGHT);
    lv_obj_set_pos(menu, 0, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(menu, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(menu, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(menu, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, LV_PART_MAIN);

    s_lvgl.run_pause_dropdown =
        osc_create_menu_item(menu, "Run", "Run\nPause", 0, 0xc62828, osc_run_pause_event_cb);
    osc_create_menu_item(menu, "Tempo", "1ms\n2ms\n5ms\n10ms\n20ms\n50ms\n100ms\n250ms\n500ms\n1s", 6, 0x455a64, osc_time_base_event_cb);
    osc_create_menu_item(menu, "Tensao", "50mV\n100mV\n200mV\n500mV\n1V\n2V\n5V\n10V", 0, 0x5d4037, osc_voltage_base_event_cb);
    osc_create_menu_item(menu, "Trigger", "Off\nNormal\nAuto\nSingle", 0, 0x6a1b9a, osc_trigger_event_cb);
    osc_create_menu_item(menu, "Borda", "Rising\nFalling", 0, 0x00838f, osc_trigger_edge_event_cb);
    osc_create_menu_item(menu, "Trig CH", "CH1\nCH2\nCH3\nCH4", 0, 0x2e7d32, osc_trigger_channel_event_cb);
    osc_create_menu_item(menu, "Cursores", "Off\nTempo\nTensao", 0, 0x37474f, osc_cursor_mode_event_cb);
    osc_create_menu_item(menu, "Brilho", "On\nOff", 1, 0x546e7a, osc_backlight_dropdown_event_cb);
    osc_create_menu_item(menu, "Tema", "Escuro\nClaro", 0, 0x263238, osc_theme_event_cb);
}

/**
 * @brief Atualiza o estilo visual de um botao de canal.
 *
 * @param[in] channel_index Indice do canal, de 0 a 3.
 */
static void osc_update_channel_button(uint8_t channel_index)
{
    lv_obj_t *button = s_lvgl.channel_buttons[channel_index];
    if (button == NULL) {
        return;
    }

    const bool visible = s_lvgl.channel_visible[channel_index];
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(visible ? OSC_CHANNEL_COLORS[channel_index] : 0x303030),
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(button,
                                lv_color_hex((visible && channel_index == 1) ? 0x000000 : 0xffffff),
                                LV_PART_MAIN);
}

/**
 * @brief Alterna a visibilidade do canal selecionado.
 *
 * @param[in] event Evento LVGL do botao.
 */
static void osc_channel_button_event_cb(lv_event_t *event)
{
    const uint8_t channel_index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (channel_index >= 4) {
        return;
    }

    s_lvgl.channel_visible[channel_index] = !s_lvgl.channel_visible[channel_index];
    osc_update_channel_button(channel_index);
    if (!s_lvgl.channel_visible[channel_index]) {
        osc_clear_measurements(channel_index);
    }
    if (s_lvgl.waveform_renderer != NULL) {
        lv_obj_invalidate(s_lvgl.waveform_renderer);
    }
}

/**
 * @brief Cria os botoes laterais de habilitacao dos canais.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_channel_buttons(lv_obj_t *parent)
{
    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t *button = lv_button_create(parent);
        s_lvgl.channel_buttons[i] = button;
        s_lvgl.channel_visible[i] = true;
        lv_obj_set_size(button, OSC_CHANNEL_BUTTON_WIDTH, OSC_CHANNEL_BUTTON_HEIGHT);
        lv_obj_set_pos(button,
                       OSC_CHANNEL_BUTTON_GAP,
                       OSC_WAVEFORM_Y + OSC_CHANNEL_BUTTON_GAP + OSC_CHANNEL_BUTTON_Y_OFFSET + (i * (OSC_CHANNEL_BUTTON_HEIGHT + OSC_CHANNEL_BUTTON_GAP)));
        lv_obj_set_style_radius(button, 7, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_add_event_cb(button, osc_channel_button_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text_fmt(label, "CH%u", (unsigned)(i + 1));
        lv_obj_center(label);
        osc_update_channel_button(i);
    }
}

/**
 * @brief Cria os quatro painéis transparentes de informações da operação.
 *
 * Os painéis são apenas visuais e ocupam o lado direito da área da waveform.
 * Seus valores serão atualizados posteriormente pela lógica do processo.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_information_panels(lv_obj_t *parent)
{
    static const char *const titles[] = {
        "Pressao",
        "Tempo\nRestante",
        "Ciclo",
        "Etapa",
    };

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t *panel = lv_obj_create(parent);
        lv_obj_remove_style_all(panel);
        lv_obj_set_size(panel, OSC_INFORMATION_PANEL_CONTENT_WIDTH, OSC_CHANNEL_BUTTON_HEIGHT);
        lv_obj_set_pos(panel,
                       OSC_INFORMATION_PANEL_X + OSC_CHANNEL_BUTTON_GAP + 2,
                       OSC_WAVEFORM_Y + OSC_CHANNEL_BUTTON_GAP + OSC_CHANNEL_BUTTON_Y_OFFSET +
                           (i * (OSC_CHANNEL_BUTTON_HEIGHT + OSC_CHANNEL_BUTTON_GAP)));
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(panel, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_border_opa(panel, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_radius(panel, 7, LV_PART_MAIN);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *title = lv_label_create(panel);
        s_lvgl.information_titles[i] = title;
        lv_label_set_text(title, titles[i]);
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(title, OSC_INFORMATION_PANEL_CONTENT_WIDTH - 8);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t *value = lv_label_create(panel);
        s_lvgl.information_values[i] = value;
        lv_label_set_text(value, "--");
        lv_obj_set_style_text_font(value, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(value, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_align(value, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
}

/**
 * @brief Formata um intervalo de tempo em uma label compacta.
 *
 * @param[in] delta_us Intervalo em microssegundos.
 * @param[out] buffer Buffer de texto.
 * @param[in] buffer_size Tamanho do buffer.
 */
static void osc_format_time(uint32_t delta_us, char *buffer, size_t buffer_size)
{
    if (delta_us >= 1000000U) {
        lv_snprintf(buffer, buffer_size, "%.2fs", (double)delta_us / 1000000.0);
    } else if (delta_us >= 1000U) {
        lv_snprintf(buffer, buffer_size, "%.2fms", (double)delta_us / 1000.0);
    } else {
        lv_snprintf(buffer, buffer_size, "%uus", (unsigned)delta_us);
    }
}

/**
 * @brief Formata uma frequencia em uma label compacta.
 *
 * @param[in] delta_us Intervalo usado para calcular a frequencia.
 * @param[out] buffer Buffer de texto.
 * @param[in] buffer_size Tamanho do buffer.
 */
static void osc_format_frequency(uint32_t delta_us, char *buffer, size_t buffer_size)
{
    if (delta_us == 0) {
        lv_snprintf(buffer, buffer_size, "--");
        return;
    }

    const double freq_hz = 1000000.0 / (double)delta_us;
    if (freq_hz >= 1000000.0) {
        lv_snprintf(buffer, buffer_size, "%.1fMHz", freq_hz / 1000000.0);
    } else if (freq_hz >= 1000.0) {
        lv_snprintf(buffer, buffer_size, "%.1fKHz", freq_hz / 1000.0);
    } else {
        lv_snprintf(buffer, buffer_size, "%.1fHz", freq_hz);
    }
}

/**
 * @brief Formata uma tensao em uma label compacta.
 *
 * @param[in] delta_mv Tensao em milivolts.
 * @param[out] buffer Buffer de texto.
 * @param[in] buffer_size Tamanho do buffer.
 */
static void osc_format_voltage(uint32_t delta_mv, char *buffer, size_t buffer_size)
{
    if (delta_mv >= 1000U) {
        lv_snprintf(buffer, buffer_size, "%.2fV", (double)delta_mv / 1000.0);
    } else {
        lv_snprintf(buffer, buffer_size, "%umV", (unsigned)delta_mv);
    }
}

/**
 * @brief Reposiciona os cursores para pontos iniciais adequados ao modo ativo.
 */
static void osc_reset_cursor_positions(void)
{
    if (s_lvgl.cursor_lines[0] == NULL || s_lvgl.cursor_lines[1] == NULL) {
        return;
    }

    if (s_lvgl.cursor_mode == OSC_CURSOR_MODE_TIME) {
        lv_obj_set_pos(s_lvgl.cursor_lines[0],
                       OSC_WAVEFORM_INNER_X + (OSC_WAVEFORM_INNER_WIDTH / 3),
                       OSC_WAVEFORM_INNER_Y);
        lv_obj_set_pos(s_lvgl.cursor_lines[1],
                       OSC_WAVEFORM_INNER_X + ((OSC_WAVEFORM_INNER_WIDTH * 2) / 3),
                       OSC_WAVEFORM_INNER_Y);
    } else if (s_lvgl.cursor_mode == OSC_CURSOR_MODE_VOLTAGE) {
        lv_obj_set_pos(s_lvgl.cursor_lines[0],
                       OSC_WAVEFORM_INNER_X,
                       OSC_WAVEFORM_INNER_Y + (OSC_WAVEFORM_INNER_HEIGHT / 3));
        lv_obj_set_pos(s_lvgl.cursor_lines[1],
                       OSC_WAVEFORM_INNER_X,
                       OSC_WAVEFORM_INNER_Y + ((OSC_WAVEFORM_INNER_HEIGHT * 2) / 3));
    }
}

/**
 * @brief Atualiza visibilidade, posicao e texto dos cursores.
 */
static void osc_update_cursor_label(void)
{
    if (s_lvgl.cursor_lines[0] == NULL || s_lvgl.cursor_lines[1] == NULL || s_lvgl.cursor_label == NULL) {
        return;
    }

    const bool time_mode = s_lvgl.paused && s_lvgl.cursor_mode == OSC_CURSOR_MODE_TIME;
    const bool voltage_mode = s_lvgl.paused && s_lvgl.cursor_mode == OSC_CURSOR_MODE_VOLTAGE;
    const bool visible = time_mode || voltage_mode;
    for (uint8_t i = 0; i < 2; i++) {
        if (visible) {
            lv_obj_clear_flag(s_lvgl.cursor_lines[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_lvgl.cursor_lines[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!visible) {
        lv_obj_add_flag(s_lvgl.cursor_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(s_lvgl.cursor_label, LV_OBJ_FLAG_HIDDEN);
    if (time_mode) {
        for (uint8_t i = 0; i < 2; i++) {
            lv_obj_set_size(s_lvgl.cursor_lines[i], 2, OSC_WAVEFORM_INNER_HEIGHT);
            lv_obj_set_y(s_lvgl.cursor_lines[i], OSC_WAVEFORM_INNER_Y);
        }

        const int32_t x0 = lv_obj_get_x(s_lvgl.cursor_lines[0]) - OSC_WAVEFORM_INNER_X;
        const int32_t x1 = lv_obj_get_x(s_lvgl.cursor_lines[1]) - OSC_WAVEFORM_INNER_X;
        const uint32_t dx = (uint32_t)LV_ABS(x1 - x0);
        const uint32_t delta_us = (dx * s_lvgl.time_base_us_per_div * OSC_TIME_DIV_COUNT) / OSC_WAVEFORM_INNER_WIDTH;
        char time_text[24] = {0};
        char freq_text[24] = {0};
        osc_format_time(delta_us, time_text, sizeof(time_text));
        osc_format_frequency(delta_us, freq_text, sizeof(freq_text));
        lv_label_set_text_fmt(s_lvgl.cursor_label, "dT %s  F %s", time_text, freq_text);
    } else if (voltage_mode) {
        for (uint8_t i = 0; i < 2; i++) {
            lv_obj_set_size(s_lvgl.cursor_lines[i], OSC_WAVEFORM_INNER_WIDTH, 2);
            lv_obj_set_x(s_lvgl.cursor_lines[i], OSC_WAVEFORM_INNER_X);
        }

        const int32_t y0 = lv_obj_get_y(s_lvgl.cursor_lines[0]) - OSC_WAVEFORM_INNER_Y;
        const int32_t y1 = lv_obj_get_y(s_lvgl.cursor_lines[1]) - OSC_WAVEFORM_INNER_Y;
        const uint32_t dy = (uint32_t)LV_ABS(y1 - y0);
        const uint32_t delta_mv = (dy * s_lvgl.voltage_base_mv_per_div * OSC_VOLTAGE_DIV_COUNT) / OSC_WAVEFORM_INNER_HEIGHT;
        char voltage_text[24] = {0};
        osc_format_voltage(delta_mv, voltage_text, sizeof(voltage_text));
        lv_label_set_text_fmt(s_lvgl.cursor_label, "dV %s", voltage_text);
    }

    if (s_lvgl.waveform_renderer != NULL) {
        lv_obj_align_to(s_lvgl.cursor_label, s_lvgl.waveform_renderer, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    }
    lv_obj_move_foreground(s_lvgl.cursor_label);
    lv_obj_move_foreground(s_lvgl.cursor_lines[0]);
    lv_obj_move_foreground(s_lvgl.cursor_lines[1]);
}

/**
 * @brief Move o cursor arrastado pelo toque.
 *
 * @param[in] event Evento LVGL da linha de cursor.
 */
static void osc_cursor_line_event_cb(lv_event_t *event)
{
    const uint8_t cursor_index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (cursor_index >= 2 || !s_lvgl.paused || s_lvgl.cursor_mode == OSC_CURSOR_MODE_OFF) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_point_t point = {0};
    lv_indev_get_point(indev, &point);
    lv_obj_t *line = s_lvgl.cursor_lines[cursor_index];

    if (s_lvgl.cursor_mode == OSC_CURSOR_MODE_TIME) {
        int32_t x = point.x;
        if (x < OSC_WAVEFORM_INNER_X) {
            x = OSC_WAVEFORM_INNER_X;
        } else if (x > OSC_WAVEFORM_INNER_X + OSC_WAVEFORM_INNER_WIDTH - 2) {
            x = OSC_WAVEFORM_INNER_X + OSC_WAVEFORM_INNER_WIDTH - 2;
        }
        lv_obj_set_x(line, x);
    } else {
        int32_t y = point.y;
        if (y < OSC_WAVEFORM_INNER_Y) {
            y = OSC_WAVEFORM_INNER_Y;
        } else if (y > OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT - 2) {
            y = OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT - 2;
        }
        lv_obj_set_y(line, y);
    }

    osc_update_cursor_label();
}

/**
 * @brief Cria os cursores de tempo/tensao sobre a area da waveform.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_cursors(lv_obj_t *parent)
{
    const int32_t initial_x[] = {
        OSC_WAVEFORM_INNER_X + (OSC_WAVEFORM_INNER_WIDTH / 3),
        OSC_WAVEFORM_INNER_X + ((OSC_WAVEFORM_INNER_WIDTH * 2) / 3),
    };
    const int32_t initial_y[] = {
        OSC_WAVEFORM_INNER_Y + (OSC_WAVEFORM_INNER_HEIGHT / 3),
        OSC_WAVEFORM_INNER_Y + ((OSC_WAVEFORM_INNER_HEIGHT * 2) / 3),
    };

    for (uint8_t i = 0; i < 2; i++) {
        lv_obj_t *line = lv_obj_create(parent);
        s_lvgl.cursor_lines[i] = line;
        lv_obj_remove_style_all(line);
        lv_obj_set_pos(line, initial_x[i], initial_y[i]);
        lv_obj_set_size(line, 2, OSC_WAVEFORM_INNER_HEIGHT);
        lv_obj_set_style_bg_color(line, lv_color_hex(osc_theme_overlay_text()), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(line, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(line, 18);
        lv_obj_add_event_cb(line, osc_cursor_line_event_cb, LV_EVENT_PRESSING, (void *)(uintptr_t)i);
        lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    }

    s_lvgl.cursor_label = lv_label_create(parent);
    lv_obj_set_width(s_lvgl.cursor_label, 240);
    lv_obj_set_style_text_color(s_lvgl.cursor_label, lv_color_hex(osc_theme_overlay_text()), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lvgl.cursor_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.cursor_label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lvgl.cursor_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(s_lvgl.cursor_label, "");
    if (s_lvgl.waveform_renderer != NULL) {
        lv_obj_align_to(s_lvgl.cursor_label, s_lvgl.waveform_renderer, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    }
    lv_obj_add_flag(s_lvgl.cursor_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Cria a linha de nivel de trigger.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_trigger_line(lv_obj_t *parent)
{
    s_lvgl.trigger_enabled = false;
    s_lvgl.trigger_rising = true;
    osc_trigger_reset_detector();
    s_lvgl.trigger_mode = 0;
    s_lvgl.trigger_channel = 0;

    s_lvgl.trigger_line = lv_obj_create(parent);
    lv_obj_remove_style_all(s_lvgl.trigger_line);
    lv_obj_set_size(s_lvgl.trigger_line, OSC_WAVEFORM_INNER_WIDTH, 2);
    lv_obj_set_pos(s_lvgl.trigger_line,
                   OSC_WAVEFORM_INNER_X,
                   OSC_WAVEFORM_INNER_Y + (OSC_WAVEFORM_INNER_HEIGHT / 2));
    lv_obj_set_style_bg_color(s_lvgl.trigger_line, lv_color_hex(0xffd600), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.trigger_line, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_lvgl.trigger_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_lvgl.trigger_line, 18);
    lv_obj_add_event_cb(s_lvgl.trigger_line, osc_trigger_line_event_cb, LV_EVENT_PRESSING, NULL);

    s_lvgl.trigger_marker = lv_obj_create(parent);
    lv_obj_remove_style_all(s_lvgl.trigger_marker);
    lv_obj_set_size(s_lvgl.trigger_marker, OSC_TRIGGER_MARKER_WIDTH, 2);
    lv_obj_set_pos(s_lvgl.trigger_marker,
                   OSC_WAVEFORM_INNER_X,
                   OSC_WAVEFORM_INNER_Y + (OSC_WAVEFORM_INNER_HEIGHT / 2));
    lv_obj_set_style_bg_color(s_lvgl.trigger_marker, lv_color_hex(0xffd600), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.trigger_marker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_lvgl.trigger_marker, LV_OBJ_FLAG_HIDDEN);

    s_lvgl.trigger_hide_timer = lv_timer_create(osc_trigger_hide_timer_cb, OSC_TRIGGER_HIDE_MS, NULL);
    if (s_lvgl.trigger_hide_timer != NULL) {
        lv_timer_pause(s_lvgl.trigger_hide_timer);
    }
}

/**
 * @brief Cria o controle vertical de brilho dentro da waveform.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_backlight_control(lv_obj_t *parent)
{
    s_lvgl.backlight_percent = OSC_BACKLIGHT_DEFAULT_PERCENT;

    s_lvgl.backlight_slider = lv_slider_create(parent);
    lv_obj_set_size(s_lvgl.backlight_slider, OSC_BACKLIGHT_SLIDER_WIDTH, OSC_BACKLIGHT_SLIDER_HEIGHT);
    lv_obj_set_pos(s_lvgl.backlight_slider,
                   OSC_WAVEFORM_INNER_X + ((OSC_WAVEFORM_INNER_WIDTH - OSC_BACKLIGHT_SLIDER_WIDTH) / 2),
                   OSC_WAVEFORM_INNER_Y + ((OSC_WAVEFORM_INNER_HEIGHT - OSC_BACKLIGHT_SLIDER_HEIGHT) / 2));
    lv_slider_set_range(s_lvgl.backlight_slider, OSC_BACKLIGHT_MIN_PERCENT, OSC_BACKLIGHT_MAX_PERCENT);
    lv_slider_set_value(s_lvgl.backlight_slider, s_lvgl.backlight_percent, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_lvgl.backlight_slider, 18, LV_PART_MAIN);
    lv_obj_set_style_radius(s_lvgl.backlight_slider, 18, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_lvgl.backlight_slider, 18, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s_lvgl.backlight_slider, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_lvgl.backlight_slider, lv_color_hex(0x4fc3f7), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_lvgl.backlight_slider, lv_color_hex(0x4fc3f7), LV_PART_KNOB);
    lv_obj_add_event_cb(s_lvgl.backlight_slider, osc_backlight_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_flag(s_lvgl.backlight_slider, LV_OBJ_FLAG_HIDDEN);

    s_lvgl.backlight_label = lv_label_create(parent);
    lv_obj_set_style_text_color(s_lvgl.backlight_label, lv_color_hex(osc_theme_overlay_text()), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lvgl.backlight_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_fmt(s_lvgl.backlight_label, "%u%%", (unsigned)s_lvgl.backlight_percent);
    lv_obj_align_to(s_lvgl.backlight_label, s_lvgl.backlight_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    lv_obj_add_flag(s_lvgl.backlight_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Converte uma amostra ADC para a coordenada vertical da waveform.
 */
static uint16_t osc_waveform_sample_to_y(uint16_t sample)
{
    if (sample > (OSC_ADC_CENTER * 2)) {
        sample = OSC_ADC_CENTER * 2;
    }
    return s_waveform_y_lookup[sample];
}

static void osc_trigger_edge_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    s_lvgl.trigger_rising = lv_dropdown_get_selected(dropdown) == 0;
    osc_trigger_reset_detector();
    if (s_lvgl.waveform_renderer != NULL) {
        lv_obj_invalidate(s_lvgl.waveform_renderer);
    }
}

static void osc_trigger_channel_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    s_lvgl.trigger_channel = (uint8_t)lv_dropdown_get_selected(dropdown);
    osc_trigger_reset_detector();
    if (s_lvgl.waveform_renderer != NULL) {
        lv_obj_invalidate(s_lvgl.waveform_renderer);
    }
}

/**
 * @brief Retorna uma amostra na ordem visual: esquerda (mais antiga) para direita (mais nova).
 */
static uint16_t osc_waveform_get_sample(uint8_t channel, uint32_t visual_index)
{
    const acquisition_history_snapshot_t snapshot = {
        .sample_count = s_lvgl.waveform_sample_count,
        .sample_head = s_lvgl.waveform_sample_head,
        .total_frames = s_lvgl.waveform_total_frames,
        .generation = s_lvgl.waveform_generation,
        .frame_rate_hz = s_lvgl.input_sample_rate_hz,
    };
    return acquisition_history_get_sample(channel, visual_index, &snapshot);
}

/**
 * @brief Insere uma amostra multicanal no historico circular da waveform.
 *
 * Esta é a fronteira entre aquisição e interface. Os frames devem chegar pelo
 * consumidor da fila SPSC, executado no core 1.
 */
static void osc_waveform_push_samples(const uint16_t samples[4])
{
    uint8_t payload[8];
    for (uint8_t channel = 0; channel < 4U; channel++) {
        payload[channel * 2U] = (uint8_t)(samples[channel] & 0xffU);
        payload[channel * 2U + 1U] = (uint8_t)(samples[channel] >> 8U);
    }
    (void)acquisition_history_push_payload(payload, 1U, s_lvgl.input_sample_rate_hz);
}

static uint32_t osc_waveform_visible_samples(void)
{
    uint64_t samples = ((uint64_t)s_lvgl.time_base_us_per_div * OSC_TIME_DIV_COUNT * s_lvgl.input_sample_rate_hz) / 1000000U;
    if (samples < 2) {
        samples = 2;
    } else if (samples > OSC_HISTORY_SAMPLE_COUNT) {
        samples = OSC_HISTORY_SAMPLE_COUNT;
    }
    return (uint32_t)samples;
}

/**
 * @brief Reinicia o envelope de colunas usado pela visualizacao em tempo real.
 *
 * O historico completo nao e alterado. O envelope e apenas uma representacao
 * estavel para os pixels atualmente desenhados na tela.
 */
static void osc_live_envelope_reset(void)
{
    s_lvgl.live_envelope_window_samples = osc_waveform_visible_samples();
    s_lvgl.live_envelope_progress = 0;
    s_lvgl.live_envelope_head = 0;
    s_lvgl.live_envelope_column_count = 0;
    s_lvgl.live_envelope_column_open = false;
}

/**
 * @brief Solicita redesenho da waveform respeitando a taxa visual máxima.
 *
 * A aquisição e o histórico continuam recebendo todos os frames; apenas a
 * invalidação do objeto LVGL é limitada para evitar uma esteira excessivamente
 * rápida e renderizações redundantes.
 */
static void osc_request_live_waveform_render(void)
{
    if (s_lvgl.waveform_renderer == NULL) {
        return;
    }
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    if (s_lvgl.last_live_render_request_us == 0U ||
        now_us - s_lvgl.last_live_render_request_us >= OSC_WAVEFORM_RENDER_INTERVAL_US) {
        s_lvgl.last_live_render_request_us = now_us;
        lv_obj_invalidate(s_lvgl.waveform_renderer);
    }
}

static void osc_update_buffer_label(void)
{
    if (s_lvgl.buffer_label != NULL) {
        if (s_lvgl.trigger_mode == 3U) {
            lv_obj_add_flag(s_lvgl.buffer_label, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        lv_obj_clear_flag(s_lvgl.buffer_label, LV_OBJ_FLAG_HIDDEN);
        uint32_t displayed_position = s_lvgl.waveform_sample_count;
        if (s_lvgl.paused && s_lvgl.pause_view_valid &&
            s_lvgl.waveform_total_frames >= s_lvgl.waveform_sample_count) {
            const uint32_t history_first = s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count;
            displayed_position = s_lvgl.pause_first_frame > history_first ?
                                 s_lvgl.pause_first_frame - history_first : 0U;
            displayed_position = displayed_position > s_lvgl.history_view_offset ?
                                 displayed_position - s_lvgl.history_view_offset : 0U;
        }
        const uint32_t percent = displayed_position * 100U / OSC_HISTORY_SAMPLE_COUNT;
        lv_label_set_text_fmt(s_lvgl.buffer_label, "Buffer: %u%%", (unsigned)percent);
    }
}

/**
 * @brief Registra a janela atualmente exibida como origem da navegacao pausada.
 */
static void osc_capture_pause_window(void)
{
    const uint32_t visible_samples = osc_waveform_visible_samples();
    const uint32_t displayed_samples = s_lvgl.waveform_sample_count < visible_samples ?
                                       s_lvgl.waveform_sample_count : visible_samples;
    if (displayed_samples == 0U || s_lvgl.waveform_total_frames < s_lvgl.waveform_sample_count) {
        s_lvgl.pause_view_valid = false;
        return;
    }

    const uint32_t history_first = s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count;
    uint32_t first_sample = s_lvgl.waveform_sample_count - displayed_samples;
    if (s_lvgl.last_plot_sample_count == displayed_samples &&
        s_lvgl.last_plot_first_sample + displayed_samples <= s_lvgl.waveform_sample_count) {
        first_sample = s_lvgl.last_plot_first_sample;
    }
    s_lvgl.pause_first_frame = history_first + first_sample;
    s_lvgl.pause_view_valid = true;
}

/**
 * @brief Alterna a pausa visual e sincroniza os controles de execucao.
 *
 * A pausa congela uma janela absoluta do historico; a aquisicao SPI pode
 * continuar preenchendo o buffer sem deslocar a waveform exibida.
 *
 * @param[in] paused @c true para pausar ou @c false para retomar.
 */
static void osc_set_paused(bool paused)
{
    if (paused && !s_lvgl.paused && !s_lvgl.pause_view_valid) {
        osc_capture_pause_window();
    }
    s_lvgl.paused = paused;
    acquisition_history_set_write_paused(paused);
    s_lvgl.history_view_offset = 0U;
    s_lvgl.history_navigation_started = false;
    if (!paused) {
        s_lvgl.pause_view_valid = false;
        if (s_lvgl.trigger_mode == 3U) {
            osc_trigger_reset_detector();
        }
        s_lvgl.cursor_mode = OSC_CURSOR_MODE_OFF;
    }
    if (s_lvgl.run_pause_dropdown != NULL &&
        lv_dropdown_get_selected(s_lvgl.run_pause_dropdown) != (paused ? 1U : 0U)) {
        lv_dropdown_set_selected(s_lvgl.run_pause_dropdown, paused ? 1U : 0U);
    }
    osc_update_buffer_label();
    osc_update_cursor_label();
    osc_update_action_buttons();
}

/**
 * @brief Navega pelo historico ao arrastar a waveform em modo Pause.
 */
static void osc_waveform_history_drag_event_cb(lv_event_t *event)
{
    if (!s_lvgl.paused || s_lvgl.trigger_mode == 3U || s_lvgl.waveform_sample_count == 0) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }
    lv_point_t point = {0};
    lv_indev_get_point(indev, &point);

    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        s_lvgl.history_drag_last_x = point.x;
        return;
    }

    const int32_t delta_x = point.x - s_lvgl.history_drag_last_x;
    s_lvgl.history_drag_last_x = point.x;
    if (delta_x == 0) {
        return;
    }

    const uint32_t visible_samples = osc_waveform_visible_samples();
    const uint32_t history_first = s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count;
    const uint32_t max_offset = s_lvgl.pause_view_valid && s_lvgl.pause_first_frame > history_first ?
                                s_lvgl.pause_first_frame - history_first :
                                (s_lvgl.waveform_sample_count > visible_samples ?
                                 s_lvgl.waveform_sample_count - visible_samples : 0U);
    const uint32_t samples_per_pixel = (visible_samples + OSC_WAVEFORM_INNER_WIDTH - 1) / OSC_WAVEFORM_INNER_WIDTH;
    const uint32_t movement = (uint32_t)abs(delta_x) * samples_per_pixel;

    if (delta_x > 0) { /* arrastar para a direita revela amostras mais antigas */
        const uint32_t available = max_offset - s_lvgl.history_view_offset;
        s_lvgl.history_view_offset += movement < available ? movement : available;
        s_lvgl.history_navigation_started |= s_lvgl.history_view_offset > 0;
    } else if (s_lvgl.history_navigation_started) {
        s_lvgl.history_view_offset = movement >= s_lvgl.history_view_offset ? 0 : s_lvgl.history_view_offset - movement;
    }
    osc_update_buffer_label();
    lv_obj_invalidate(s_lvgl.waveform_renderer);
}

static void osc_waveform_set_pixel(uint16_t *framebuffer, uint32_t stride_px, const lv_area_t *buffer_area, const lv_area_t *clip_area, int32_t x, int32_t y, uint16_t color)
{
    if (x >= OSC_WAVEFORM_INNER_X && x < OSC_WAVEFORM_INNER_X + OSC_WAVEFORM_INNER_WIDTH &&
        y >= OSC_WAVEFORM_INNER_Y && y < OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT &&
        x >= clip_area->x1 && x <= clip_area->x2 && y >= clip_area->y1 && y <= clip_area->y2) {
        framebuffer[((y - buffer_area->y1) * stride_px) + x - buffer_area->x1] = color;
    }
}

static void osc_waveform_draw_line(uint16_t *framebuffer, uint32_t stride_px, const lv_area_t *buffer_area, const lv_area_t *clip_area, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    int32_t dx = abs(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -abs(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;

    while (true) {
        osc_waveform_set_pixel(framebuffer, stride_px, buffer_area, clip_area, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int32_t twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

/**
 * @brief Reinicia a busca incremental por uma nova borda de trigger.
 *
 * A captura retida pelo modo Normal nao e alterada. Isso permite ajustar o
 * nivel do trigger sem apagar a ultima janela valida enquanto nao ha uma nova
 * borda no nivel escolhido.
 */
static void osc_trigger_reset_search(void)
{
    s_lvgl.trigger_scan_total_frames = 0U;
    s_lvgl.trigger_latest_event_frame = 0U;
    s_lvgl.trigger_candidate_event_frame = 0U;
    s_lvgl.trigger_scan_initialized = false;
    s_lvgl.trigger_latest_event_valid = false;
    s_lvgl.trigger_candidate_event_valid = false;
}

/** @brief Libera a cópia independente usada pela captura do modo Single. */
static void osc_trigger_clear_single_capture(void)
{
    for (uint8_t channel = 0U; channel < 4U; channel++) {
        heap_caps_free(s_lvgl.trigger_single_samples[channel]);
        s_lvgl.trigger_single_samples[channel] = NULL;
    }
    s_lvgl.trigger_single_sample_count = 0U;
    s_lvgl.trigger_single_capture_pending = false;
    s_lvgl.trigger_single_pending_first_frame = 0U;
    s_lvgl.trigger_single_pending_sample_count = 0U;
}

/**
 * @brief Reinicia completamente a detecção incremental de borda.
 *
 * Alem da busca por eventos, invalida a janela retida do modo Normal. Deve
 * ser usada quando a referencia da captura deixa de ser compativel, como em
 * troca de canal, borda, modo ou historico.
 */
static void osc_trigger_reset_detector(void)
{
    if (s_lvgl.trigger_single_capture_pending && !s_lvgl.paused) {
        acquisition_history_set_write_paused(false);
    }
    osc_trigger_reset_search();
    osc_trigger_clear_single_capture();
    s_lvgl.trigger_single_captured = false;
    s_lvgl.trigger_single_first_frame = 0U;
    s_lvgl.trigger_normal_first_frame = 0U;
    s_lvgl.trigger_normal_hold_valid = false;
}

/**
 * @brief Examina somente frames novos em busca da borda de trigger.
 *
 * A rotina roda fora do callback de desenho para que o modo AUTO sem borda
 * permaneça em varredura livre sem reler toda a janela de PSRAM a cada frame.
 */
static void osc_trigger_scan_new_samples(void)
{
    if (!s_lvgl.trigger_enabled || s_lvgl.paused || s_lvgl.trigger_channel >= 4U ||
        s_lvgl.waveform_sample_count < 2U) {
        return;
    }

    const uint32_t total = s_lvgl.waveform_total_frames;
    const uint32_t history_first = total - s_lvgl.waveform_sample_count;
    const uint32_t visible_samples = osc_waveform_visible_samples();
    const uint32_t right_samples = visible_samples - (visible_samples / 2U) - 1U;
    uint32_t scan_first = s_lvgl.trigger_scan_initialized ? s_lvgl.trigger_scan_total_frames :
                                                          (total > 0U ? total - 1U : total);
    if (scan_first < history_first) {
        scan_first = history_first;
    }
    if (total - scan_first > OSC_TRIGGER_SCAN_MAX_SAMPLES) {
        scan_first = total - OSC_TRIGGER_SCAN_MAX_SAMPLES;
    }

    int32_t threshold = lv_obj_get_y(s_lvgl.trigger_line) - OSC_WAVEFORM_INNER_Y;
    if (threshold < 0) {
        threshold = 0;
    } else if (threshold >= OSC_WAVEFORM_INNER_HEIGHT) {
        threshold = OSC_WAVEFORM_INNER_HEIGHT - 1;
    }
    const int32_t hysteresis = 4;
    for (uint32_t frame = scan_first; frame < total; frame++) {
        const uint32_t visual_index = frame - history_first;
        if (visual_index == 0U || visual_index >= s_lvgl.waveform_sample_count) {
            continue;
        }
        const int32_t previous = osc_waveform_sample_to_y(
            osc_waveform_get_sample(s_lvgl.trigger_channel, visual_index - 1U));
        const int32_t current = osc_waveform_sample_to_y(
            osc_waveform_get_sample(s_lvgl.trigger_channel, visual_index));
        const bool crossed = s_lvgl.trigger_rising ?
                             (previous >= threshold + hysteresis && current <= threshold - hysteresis) :
                             (previous <= threshold - hysteresis && current >= threshold + hysteresis);
        if (crossed && !s_lvgl.trigger_candidate_event_valid) {
            s_lvgl.trigger_candidate_event_frame = frame;
            s_lvgl.trigger_candidate_event_valid = true;
        }
    }
    if (s_lvgl.trigger_candidate_event_valid &&
        total > s_lvgl.trigger_candidate_event_frame + right_samples) {
        s_lvgl.trigger_latest_event_frame = s_lvgl.trigger_candidate_event_frame;
        s_lvgl.trigger_latest_event_valid = true;
        s_lvgl.trigger_candidate_event_valid = false;
    }
    if (s_lvgl.trigger_latest_event_valid && s_lvgl.trigger_latest_event_frame < history_first) {
        s_lvgl.trigger_latest_event_valid = false;
    }
    s_lvgl.trigger_scan_total_frames = total;
    s_lvgl.trigger_scan_initialized = true;
}

/**
 * @brief Retorna a borda incremental mais recente quando já há pós-trigger suficiente.
 */
static bool osc_waveform_find_trigger(uint32_t displayed_samples, uint32_t *first_sample)
{
    if (!s_lvgl.trigger_enabled || s_lvgl.paused || s_lvgl.trigger_channel >= 4 ||
        s_lvgl.waveform_sample_count < displayed_samples || !s_lvgl.trigger_latest_event_valid) {
        return false;
    }
    const uint32_t left_samples = displayed_samples / 2;
    const uint32_t right_samples = displayed_samples - left_samples - 1;
    const uint32_t history_first = s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count;
    const uint32_t event = s_lvgl.trigger_latest_event_frame;
    if (event < history_first + left_samples || event + right_samples >= s_lvgl.waveform_total_frames) {
        return false;
    }
    *first_sample = event - history_first - left_samples;
    return true;
}

/**
 * @brief Congela a janela centralizada quando o modo Single encontra uma borda.
 *
 * A janela e armazenada em coordenada absoluta do historico para continuar
 * representando o mesmo sinal mesmo que a aquisicao continue preenchendo o
 * ring buffer em PSRAM.
 */
static void osc_trigger_capture_single(void)
{
    if (s_lvgl.trigger_mode != 3U || !s_lvgl.trigger_enabled || s_lvgl.paused ||
        s_lvgl.trigger_single_captured) {
        return;
    }

    if (!s_lvgl.trigger_single_capture_pending) {
        const uint32_t displayed_samples = osc_waveform_visible_samples();
        if (!s_lvgl.trigger_latest_event_valid || s_lvgl.waveform_sample_count < displayed_samples) {
            return;
        }

        const uint32_t history_first = s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count;
        const uint32_t left_samples = displayed_samples / 2U;
        const uint32_t right_samples = displayed_samples - left_samples - 1U;
        const uint32_t event_frame = s_lvgl.trigger_latest_event_frame;
        if (event_frame < history_first + left_samples ||
            event_frame + right_samples >= s_lvgl.waveform_total_frames) {
            return;
        }

        s_lvgl.trigger_single_pending_first_frame = event_frame - left_samples;
        s_lvgl.trigger_single_pending_sample_count = displayed_samples;
        s_lvgl.trigger_single_capture_pending = true;
        acquisition_history_set_write_paused(true);
        return;
    }

    if (!acquisition_history_is_write_pause_confirmed()) {
        return;
    }

    const uint32_t displayed_samples = s_lvgl.trigger_single_pending_sample_count;
    const uint32_t history_first = s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count;
    if (displayed_samples == 0U || s_lvgl.trigger_single_pending_first_frame < history_first ||
        s_lvgl.trigger_single_pending_first_frame + displayed_samples > s_lvgl.waveform_total_frames) {
        s_lvgl.trigger_single_capture_pending = false;
        acquisition_history_set_write_paused(false);
        return;
    }

    s_lvgl.trigger_single_first_frame = s_lvgl.trigger_single_pending_first_frame;
    const uint32_t first_sample = s_lvgl.trigger_single_first_frame - history_first;
    for (uint8_t channel = 0U; channel < 4U; channel++) {
        s_lvgl.trigger_single_samples[channel] = heap_caps_malloc(
            displayed_samples * sizeof(*s_lvgl.trigger_single_samples[channel]), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_lvgl.trigger_single_samples[channel] == NULL) {
            ESP_LOGE(TAG, "sem PSRAM para captura Single");
            osc_trigger_clear_single_capture();
            acquisition_history_set_write_paused(false);
            return;
        }
        for (uint32_t sample = 0U; sample < displayed_samples; sample++) {
            s_lvgl.trigger_single_samples[channel][sample] =
                osc_waveform_get_sample(channel, first_sample + sample);
        }
    }
    s_lvgl.trigger_single_sample_count = displayed_samples;
    s_lvgl.trigger_single_capture_pending = false;
    s_lvgl.trigger_single_captured = true;
    s_lvgl.pause_first_frame = s_lvgl.trigger_single_first_frame;
    s_lvgl.pause_view_valid = true;
    osc_set_paused(true);
}

/**
 * @brief Desenha diretamente no framebuffer RGB565 pertencente a camada LVGL atual.
 *
 * O objeto fica abaixo dos cursores e controles LVGL. Assim, os objetos sobrepostos
 * continuam sendo compostos normalmente depois deste callback.
 */
static void osc_waveform_draw_event_cb(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    if (layer == NULL || layer->draw_buf == NULL || layer->draw_buf->data == NULL) {
        return;
    }

    uint16_t *framebuffer = (uint16_t *)layer->draw_buf->data;
    const uint32_t stride_px = layer->draw_buf->header.stride / sizeof(uint16_t);
    const lv_area_t *clip_area = &layer->_clip_area;
    const lv_area_t *buffer_area = &layer->buf_area;
    const uint16_t background = lv_color_to_u16(lv_color_hex(osc_theme_waveform_bg()));
    const uint16_t dot = lv_color_to_u16(lv_color_hex(osc_theme_grid_dot()));
    const uint16_t center = lv_color_to_u16(lv_color_hex(osc_theme_grid_center()));

    const int32_t render_x1 = clip_area->x1 > OSC_WAVEFORM_INNER_X ? clip_area->x1 : OSC_WAVEFORM_INNER_X;
    const int32_t render_y1 = clip_area->y1 > OSC_WAVEFORM_INNER_Y ? clip_area->y1 : OSC_WAVEFORM_INNER_Y;
    const int32_t render_x2 = clip_area->x2 < OSC_WAVEFORM_INNER_X + OSC_WAVEFORM_INNER_WIDTH - 1 ?
                              clip_area->x2 : OSC_WAVEFORM_INNER_X + OSC_WAVEFORM_INNER_WIDTH - 1;
    const int32_t render_y2 = clip_area->y2 < OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT - 1 ?
                              clip_area->y2 : OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT - 1;
    for (int32_t y = render_y1; y <= render_y2; y++) {
        uint16_t *row = framebuffer + ((y - buffer_area->y1) * stride_px) + render_x1 - buffer_area->x1;
        for (int32_t x = render_x1; x <= render_x2; x++) {
            *row++ = background;
        }
    }

    for (int32_t horizontal_index = 1; horizontal_index < 8; horizontal_index++) {
        if (horizontal_index != 4) {
            const int32_t y = OSC_WAVEFORM_INNER_Y + (horizontal_index * 46);
            for (int32_t x = OSC_WAVEFORM_INNER_X; x < OSC_WAVEFORM_INNER_X + OSC_WAVEFORM_INNER_WIDTH; x += 8) {
                osc_waveform_set_pixel(framebuffer, stride_px, buffer_area, clip_area, x, y, dot);
            }
        }
    }
    for (int32_t vertical_index = 1; vertical_index < 10; vertical_index++) {
        if (vertical_index != 5) {
            const int32_t x = OSC_WAVEFORM_X + (vertical_index * OSC_WAVEFORM_VERTICAL_DIVISION_WIDTH);
            for (int32_t y = OSC_WAVEFORM_INNER_Y; y < OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT; y += 8) {
                osc_waveform_set_pixel(framebuffer, stride_px, buffer_area, clip_area, x, y, dot);
            }
        }
    }
    for (int32_t x = OSC_WAVEFORM_INNER_X; x < OSC_WAVEFORM_INNER_X + OSC_WAVEFORM_INNER_WIDTH; x += 4) {
        osc_waveform_set_pixel(framebuffer, stride_px, buffer_area, clip_area, x, OSC_WAVEFORM_INNER_Y + (OSC_WAVEFORM_INNER_HEIGHT / 2), center);
    }
    for (int32_t y = OSC_WAVEFORM_INNER_Y; y < OSC_WAVEFORM_INNER_Y + OSC_WAVEFORM_INNER_HEIGHT; y += 4) {
        osc_waveform_set_pixel(framebuffer, stride_px, buffer_area, clip_area, OSC_WAVEFORM_CENTER_X, y, center);
    }

    if (false && !s_lvgl.paused && !s_lvgl.trigger_enabled && s_lvgl.live_envelope_column_count > 0U &&
        s_lvgl.live_envelope_min != NULL && s_lvgl.live_envelope_max != NULL &&
        s_lvgl.live_envelope_first != NULL && s_lvgl.live_envelope_last != NULL) {
        for (uint16_t visual_column = 0; visual_column < s_lvgl.live_envelope_column_count; visual_column++) {
            const uint16_t column = (uint16_t)((s_lvgl.live_envelope_head + visual_column) % OSC_WAVEFORM_INNER_WIDTH);
            const int32_t x = OSC_WAVEFORM_INNER_X + visual_column;
            for (uint8_t channel = 0; channel < 4; channel++) {
                if (!s_lvgl.channel_visible[channel]) {
                    continue;
                }
                const size_t offset = (size_t)channel * OSC_WAVEFORM_INNER_WIDTH + column;
                const uint16_t color = lv_color_to_u16(lv_color_hex(OSC_CHANNEL_COLORS[channel]));
                if (visual_column > 0U) {
                    const uint16_t previous_column = (uint16_t)((s_lvgl.live_envelope_head + visual_column - 1U) %
                                                                OSC_WAVEFORM_INNER_WIDTH);
                    const size_t previous_offset = (size_t)channel * OSC_WAVEFORM_INNER_WIDTH + previous_column;
                    osc_waveform_draw_line(framebuffer, stride_px, buffer_area, clip_area, x - 1,
                                           OSC_WAVEFORM_INNER_Y + osc_waveform_sample_to_y(s_lvgl.live_envelope_last[previous_offset]), x,
                                           OSC_WAVEFORM_INNER_Y + osc_waveform_sample_to_y(s_lvgl.live_envelope_first[offset]), color);
                }
                osc_waveform_draw_line(framebuffer, stride_px, buffer_area, clip_area, x,
                                       OSC_WAVEFORM_INNER_Y + osc_waveform_sample_to_y(s_lvgl.live_envelope_min[offset]), x,
                                       OSC_WAVEFORM_INNER_Y + osc_waveform_sample_to_y(s_lvgl.live_envelope_max[offset]), color);
            }
        }
        s_lvgl.last_plot_sample_count = 0;
        s_lvgl.last_plot_triggered = false;
        return;
    }

    const uint32_t visible_samples = osc_waveform_visible_samples();
    if (s_lvgl.waveform_sample_count < 2) {
        return;
    }
    const uint32_t displayed_samples = s_lvgl.waveform_sample_count < visible_samples ? s_lvgl.waveform_sample_count : visible_samples;
    const uint32_t max_offset = s_lvgl.waveform_sample_count - displayed_samples;
    const uint32_t view_offset = s_lvgl.history_view_offset < max_offset ? s_lvgl.history_view_offset : max_offset;
    uint32_t first_sample = max_offset - view_offset;
    bool draw_signal = true;
    bool plot_triggered = false;
    const uint32_t history_first = s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count;
    const bool single_capture_visible = s_lvgl.paused && s_lvgl.trigger_mode == 3U &&
                                        s_lvgl.trigger_single_captured &&
                                        s_lvgl.trigger_single_sample_count == displayed_samples;
    if (single_capture_visible) {
        first_sample = 0U;
        plot_triggered = true;
    } else if (s_lvgl.paused && s_lvgl.pause_view_valid) {
        if (s_lvgl.pause_first_frame >= history_first &&
            s_lvgl.pause_first_frame + displayed_samples <= s_lvgl.waveform_total_frames) {
            const uint32_t pause_offset = s_lvgl.history_view_offset <
                                          s_lvgl.pause_first_frame - history_first ?
                                          s_lvgl.history_view_offset : s_lvgl.pause_first_frame - history_first;
            first_sample = s_lvgl.pause_first_frame - history_first - pause_offset;
            plot_triggered = true;
        } else {
            draw_signal = false;
        }
    } else {
        const bool trigger_active = !s_lvgl.paused && s_lvgl.time_base_us_per_div <= 100000U &&
                                    displayed_samples == visible_samples && s_lvgl.trigger_enabled;
        if (trigger_active) {
            const bool trigger_found = osc_waveform_find_trigger(displayed_samples, &first_sample);
            if (trigger_found) {
                if (s_lvgl.trigger_mode == 1U) {
                    s_lvgl.trigger_normal_first_frame =
                        s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count + first_sample;
                    s_lvgl.trigger_normal_hold_valid = true;
                }
                plot_triggered = true;
            } else if (s_lvgl.trigger_mode == 1U) {
                const uint32_t history_first = s_lvgl.waveform_total_frames - s_lvgl.waveform_sample_count;
                if (s_lvgl.trigger_normal_hold_valid &&
                    s_lvgl.trigger_normal_first_frame >= history_first &&
                    s_lvgl.trigger_normal_first_frame + displayed_samples <= s_lvgl.waveform_total_frames) {
                    first_sample = s_lvgl.trigger_normal_first_frame - history_first;
                    plot_triggered = true;
                } else {
                    draw_signal = false;
                }
            } else if (s_lvgl.trigger_mode != 2U) {
                draw_signal = false;
            }
        }
    }
    if (!draw_signal) {
        s_lvgl.last_plot_sample_count = 0;
        s_lvgl.last_plot_triggered = false;
        return;
    }
    s_lvgl.last_plot_first_sample = first_sample;
    s_lvgl.last_plot_sample_count = displayed_samples;
    s_lvgl.last_plot_triggered = plot_triggered;
    const uint16_t first_x = OSC_WAVEFORM_INNER_X;
    const uint16_t rendered_width = 1 + (uint16_t)(((uint64_t)(displayed_samples - 1) * (OSC_CHART_POINT_COUNT - 1)) /
                                                    (visible_samples - 1));
    if (rendered_width < 2U) {
        return;
    }

    bool previous_valid[4] = {false};
    uint16_t previous_last[4] = {0};
    for (uint16_t column = 0; column < rendered_width; column++) {
        const uint32_t source_first = first_sample +
                                      (((uint64_t)column * (displayed_samples - 1)) / (rendered_width - 1));
        uint32_t source_last = first_sample +
                               (((uint64_t)(column + 1U) * (displayed_samples - 1)) / (rendered_width - 1));
        const uint32_t last_displayed_sample = first_sample + displayed_samples - 1U;
        if (source_last > last_displayed_sample) {
            source_last = last_displayed_sample;
        }
        const int32_t x = first_x + column;
        for (uint8_t channel = 0; channel < 4; channel++) {
            if (!s_lvgl.channel_visible[channel]) {
                continue;
            }
            uint16_t minimum = single_capture_visible ? s_lvgl.trigger_single_samples[channel][source_first] :
                                                       osc_waveform_get_sample(channel, source_first);
            uint16_t maximum = minimum;
            const uint16_t first_value = minimum;
            uint16_t last_value = minimum;
            for (uint32_t source = source_first + 1U; source <= source_last; source++) {
                const uint16_t value = single_capture_visible ? s_lvgl.trigger_single_samples[channel][source] :
                                                               osc_waveform_get_sample(channel, source);
                if (value < minimum) {
                    minimum = value;
                }
                if (value > maximum) {
                    maximum = value;
                }
                last_value = value;
            }
            const uint16_t color = lv_color_to_u16(lv_color_hex(OSC_CHANNEL_COLORS[channel]));
            if (previous_valid[channel]) {
                osc_waveform_draw_line(framebuffer, stride_px, buffer_area, clip_area, x - 1,
                                       OSC_WAVEFORM_INNER_Y + osc_waveform_sample_to_y(previous_last[channel]), x,
                                       OSC_WAVEFORM_INNER_Y + osc_waveform_sample_to_y(first_value), color);
            }
            osc_waveform_draw_line(framebuffer, stride_px, buffer_area, clip_area, x,
                                   OSC_WAVEFORM_INNER_Y + osc_waveform_sample_to_y(minimum), x,
                                   OSC_WAVEFORM_INNER_Y + osc_waveform_sample_to_y(maximum), color);
            previous_last[channel] = last_value;
            previous_valid[channel] = true;
        }
    }
}

/**
 * @brief Limpa o estado da varredura incremental da waveform.
 */
static void osc_waveform_reset(void)
{
    s_lvgl.waveform_sample_count = 0;
    s_lvgl.waveform_sample_head = 0;
    s_lvgl.waveform_total_frames = 0;
    s_lvgl.waveform_generation = 0;
    s_lvgl.history_view_offset = 0;
    s_lvgl.history_navigation_started = false;
    s_lvgl.pause_first_frame = 0U;
    s_lvgl.pause_view_valid = false;
    osc_trigger_reset_detector();
    s_lvgl.last_plot_first_sample = 0;
    s_lvgl.last_plot_sample_count = 0;
    s_lvgl.last_plot_triggered = false;
    s_lvgl.last_live_render_request_us = 0;
    osc_live_envelope_reset();
    for (uint8_t channel = 0; channel < 4; channel++) {
        osc_clear_measurements(channel);
    }
    osc_update_buffer_label();
}

/**
 * @brief Cria o objeto que desenha a waveform diretamente no framebuffer RGB565.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_waveform_renderer(lv_obj_t *parent)
{
    for (uint16_t sample = 0; sample <= (OSC_ADC_CENTER * 2); sample++) {
        s_waveform_y_lookup[sample] = (uint16_t)((((OSC_ADC_CENTER * 2) - sample) * (OSC_WAVEFORM_INNER_HEIGHT - 1)) /
                                                  (OSC_ADC_CENTER * 2));
    }
    s_lvgl.live_envelope_min = heap_caps_malloc(OSC_LIVE_ENVELOPE_VALUE_COUNT * sizeof(uint16_t),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_lvgl.live_envelope_max = heap_caps_malloc(OSC_LIVE_ENVELOPE_VALUE_COUNT * sizeof(uint16_t),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_lvgl.live_envelope_first = heap_caps_malloc(OSC_LIVE_ENVELOPE_VALUE_COUNT * sizeof(uint16_t),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_lvgl.live_envelope_last = heap_caps_malloc(OSC_LIVE_ENVELOPE_VALUE_COUNT * sizeof(uint16_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lvgl.live_envelope_min == NULL || s_lvgl.live_envelope_max == NULL ||
        s_lvgl.live_envelope_first == NULL || s_lvgl.live_envelope_last == NULL) {
        ESP_LOGW(TAG, "sem PSRAM para envelope visual; usando renderer legado");
        heap_caps_free(s_lvgl.live_envelope_min);
        heap_caps_free(s_lvgl.live_envelope_max);
        heap_caps_free(s_lvgl.live_envelope_first);
        heap_caps_free(s_lvgl.live_envelope_last);
        s_lvgl.live_envelope_min = NULL;
        s_lvgl.live_envelope_max = NULL;
        s_lvgl.live_envelope_first = NULL;
        s_lvgl.live_envelope_last = NULL;
    }

    s_lvgl.waveform_renderer = lv_obj_create(parent);
    lv_obj_remove_style_all(s_lvgl.waveform_renderer);
    lv_obj_set_size(s_lvgl.waveform_renderer, OSC_WAVEFORM_INNER_WIDTH, OSC_WAVEFORM_INNER_HEIGHT);
    lv_obj_set_pos(s_lvgl.waveform_renderer, OSC_WAVEFORM_INNER_X, OSC_WAVEFORM_INNER_Y);
    lv_obj_add_flag(s_lvgl.waveform_renderer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_lvgl.waveform_renderer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_lvgl.waveform_renderer, osc_waveform_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_lvgl.waveform_renderer, osc_waveform_history_drag_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_lvgl.waveform_renderer, osc_waveform_history_drag_event_cb, LV_EVENT_PRESSING, NULL);

    s_lvgl.buffer_label = lv_label_create(parent);
    lv_obj_set_style_text_color(s_lvgl.buffer_label, lv_color_hex(osc_theme_overlay_text()), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lvgl.buffer_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_lvgl.buffer_label, lv_color_hex(osc_theme_waveform_bg()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.buffer_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_lvgl.buffer_label, 2, LV_PART_MAIN);
    lv_obj_align_to(s_lvgl.buffer_label, s_lvgl.waveform_renderer, LV_ALIGN_TOP_RIGHT, -72, 8);

    s_lvgl.time_base_us_per_div = OSC_DEFAULT_TIME_BASE_US_PER_DIV;
    s_lvgl.voltage_base_mv_per_div = OSC_DEFAULT_VOLTAGE_BASE_MV_PER_DIV;
    s_lvgl.input_sample_rate_hz = OSC_DEFAULT_INPUT_SAMPLE_RATE_HZ;
    osc_waveform_reset();
    s_lvgl.paused = false;
}

/**
 * @brief Cria a area de waveform com borda e grade pontilhada.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_waveform_area(lv_obj_t *parent)
{
    lv_obj_t *waveform = lv_obj_create(parent);
    lv_obj_remove_style_all(waveform);
    lv_obj_set_size(waveform, OSC_WAVEFORM_WIDTH, OSC_WAVEFORM_HEIGHT);
    lv_obj_set_pos(waveform, OSC_WAVEFORM_X, OSC_WAVEFORM_Y);
    lv_obj_set_style_bg_color(waveform, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(waveform, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(waveform, OSC_WAVEFORM_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_color(waveform, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_radius(waveform, 0, LV_PART_MAIN);

    osc_create_waveform_renderer(parent);
    osc_create_cursors(parent);
    osc_create_trigger_line(parent);
    osc_create_backlight_control(parent);
}

/** @brief Atualiza as cores dos botões de Pausar e Parar Ciclo. */
static void osc_update_action_buttons(void)
{
    if (s_lvgl.pause_button != NULL) {
        const uint32_t pause_color = s_lvgl.paused ? 0x00c853 : 0x000000;
        lv_obj_set_style_bg_color(s_lvgl.pause_button, lv_color_hex(pause_color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_lvgl.pause_button, s_lvgl.paused ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    }
    if (s_lvgl.pause_button_label != NULL) {
        lv_label_set_text(s_lvgl.pause_button_label, s_lvgl.paused ? "Pausado" : "Pausar");
    }
    if (s_lvgl.stop_cycle_button != NULL) {
        const uint32_t stop_color = s_lvgl.stop_cycle_requested && !s_lvgl.stop_cycle_blink_on ? 0x5f0000 : 0xc62828;
        lv_obj_set_style_bg_color(s_lvgl.stop_cycle_button, lv_color_hex(stop_color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_lvgl.stop_cycle_button, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (s_lvgl.stop_cycle_button_label != NULL) {
        lv_label_set_text(s_lvgl.stop_cycle_button_label,
                          s_lvgl.stop_cycle_requested ? "Ciclo Pausado" : "Parar Ciclo");
    }
}

/**
 * @brief Alterna a indicação visual do pedido de parada de ciclo.
 *
 * @param[in] timer Timer LVGL de pisca.
 */
static void osc_stop_cycle_blink_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_lvgl.stop_cycle_requested) {
        return;
    }
    s_lvgl.stop_cycle_blink_on = !s_lvgl.stop_cycle_blink_on;
    osc_update_action_buttons();
}

/**
 * @brief Alterna a pausa da aquisição quando não há pedido de parada de ciclo.
 *
 * @param[in] event Evento LVGL do botão Pausar.
 */
static void osc_pause_button_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_lvgl.stop_cycle_requested) {
        return;
    }
    osc_set_paused(!s_lvgl.paused);
}

/**
 * @brief Pausa ou retoma o ciclo PWM quando a visualização não está pausada.
 *
 * @param[in] event Evento LVGL do botão Parar Ciclo.
 */
static void osc_stop_cycle_button_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_lvgl.paused) {
        return;
    }
    const bool pause_cycle = !s_lvgl.stop_cycle_requested;
    if (s_cycle_pause_callback == NULL || !s_cycle_pause_callback(pause_cycle)) {
        return;
    }
    s_lvgl.stop_cycle_requested = pause_cycle;
    s_lvgl.stop_cycle_blink_on = pause_cycle;
    osc_update_action_buttons();
}

/**
 * @brief Retorna ao menu principal pela ação registrada pela aplicação.
 *
 * @param[in] event Evento LVGL do botão Menu.
 */
static void osc_menu_button_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_menu_callback != NULL) {
        s_menu_callback();
    }
}

/**
 * @brief Encerra manualmente o ciclo pela ação registrada pela aplicação.
 *
 * @param[in] event Evento LVGL do botão Terminar Ciclo.
 */
static void osc_cycle_finished_button_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_cycle_finished_callback != NULL) {
        s_cycle_finished_callback(false);
    }
}

/**
 * @brief Cria os botões de ação roláveis na metade inferior direita.
 *
 * @param[in] parent Tela principal do osciloscópio.
 */
static void osc_create_action_buttons(lv_obj_t *parent)
{
    static const char *const labels[] = {"Pausar", "Parar Ciclo", "Menu", "Terminar Ciclo"};
    static lv_event_cb_t const callbacks[] = {
        osc_pause_button_event_cb,
        osc_stop_cycle_button_event_cb,
        osc_menu_button_event_cb,
        osc_cycle_finished_button_event_cb,
    };
    lv_obj_t **const buttons[] = {
        &s_lvgl.pause_button,
        &s_lvgl.stop_cycle_button,
        NULL,
        NULL,
    };
    lv_obj_t **const button_labels[] = {
        &s_lvgl.pause_button_label,
        &s_lvgl.stop_cycle_button_label,
        NULL,
        NULL,
    };
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, OSC_ACTION_PANEL_WIDTH, OSC_ACTION_BUTTON_HEIGHT + 8);
    lv_obj_set_pos(container, OSC_ACTION_PANEL_X, OSC_ACTION_BUTTON_Y - 4);
    lv_obj_set_scroll_dir(container, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);

    for (uint8_t index = 0; index < 4; index++) {
        lv_obj_t *button = lv_button_create(container);
        lv_obj_set_size(button, OSC_ACTION_BUTTON_WIDTH, OSC_ACTION_BUTTON_HEIGHT);
        lv_obj_set_pos(button, OSC_ACTION_BUTTON_GAP + index * (OSC_ACTION_BUTTON_WIDTH + OSC_ACTION_BUTTON_GAP), 4);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(0x606060), LV_PART_MAIN);
        lv_obj_set_style_radius(button, 5, LV_PART_MAIN);
        lv_obj_add_event_cb(button, callbacks[index], LV_EVENT_CLICKED, NULL);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, labels[index]);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_center(label);
        if (buttons[index] != NULL) {
            *buttons[index] = button;
            *button_labels[index] = label;
        } else {
            lv_obj_set_style_bg_color(button, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
        }
    }
    s_lvgl.stop_cycle_blink_timer = lv_timer_create(osc_stop_cycle_blink_timer_cb, 500, NULL);
    osc_update_action_buttons();
}

/**
 * @brief Cria uma linha de medicoes de canal.
 *
 * @param[in] parent Tela principal.
 * @param[in] channel_index Indice do canal, de 0 a 3.
 * A linha é inserida no container vertical de medições da metade esquerda.
 *
 * @param[in] y Coordenada vertical da linha.
 */
static void osc_create_measurement_row(lv_obj_t *parent, uint8_t channel_index, int32_t y)
{
    const char *texts[] = {
        "RMS 0.00",
        "PK+ 0.00",
        "PK- 0.00",
        "FREQ 0.0 Hz",
        "DUTY 0%",
    };
    const lv_color_t text_color = lv_color_hex(channel_index == 1 ? 0x000000 : 0xffffff);

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, OSC_CHANNEL_INFO_WIDTH, OSC_CHANNEL_ROW_HEIGHT);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(row, lv_color_hex(OSC_CHANNEL_COLORS[channel_index]), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);

    for (size_t i = 0; i < (sizeof(texts) / sizeof(texts[0])); i++) {
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, texts[i]);
        s_lvgl.measurement_labels[channel_index][i] = label;
        lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, OSC_MEASUREMENT_X[i], 0);
    }
}

static uint32_t osc_counts_to_mv(uint32_t counts)
{
    return (counts * s_lvgl.voltage_base_mv_per_div) / OSC_ADC_COUNTS_PER_DIV;
}

static void osc_clear_measurements(uint8_t channel)
{
    if (channel >= 4U || s_lvgl.measurement_labels[channel][0] == NULL) {
        return;
    }
    lv_label_set_text(s_lvgl.measurement_labels[channel][0], "RMS 0.00");
    lv_label_set_text(s_lvgl.measurement_labels[channel][1], "PK+ 0.00");
    lv_label_set_text(s_lvgl.measurement_labels[channel][2], "PK- 0.00");
    lv_label_set_text(s_lvgl.measurement_labels[channel][3], "FREQ 0.0Hz");
    lv_label_set_text(s_lvgl.measurement_labels[channel][4], "DUTY 0%");
}

static void osc_update_frequency_duty(uint8_t channel, uint32_t first_sample, uint32_t sample_count)
{
    if (channel != s_lvgl.trigger_channel || !s_lvgl.trigger_enabled || !s_lvgl.last_plot_triggered) {
        lv_label_set_text(s_lvgl.measurement_labels[channel][3], "FREQ --");
        lv_label_set_text(s_lvgl.measurement_labels[channel][4], "DUTY --");
        return;
    }

    uint32_t previous_rising = 0;
    uint32_t last_rising = 0;
    const uint16_t lower_threshold = OSC_ADC_CENTER - 16;
    const uint16_t upper_threshold = OSC_ADC_CENTER + 16;
    for (uint32_t index = 1; index < sample_count; index++) {
        const uint16_t previous = osc_waveform_get_sample(channel, first_sample + index - 1);
        const uint16_t current = osc_waveform_get_sample(channel, first_sample + index);
        if (previous <= lower_threshold && current >= upper_threshold) {
            previous_rising = last_rising;
            last_rising = index;
        }
    }

    if (previous_rising == 0 || last_rising <= previous_rising) {
        lv_label_set_text(s_lvgl.measurement_labels[channel][3], "FREQ --");
        lv_label_set_text(s_lvgl.measurement_labels[channel][4], "DUTY --");
        return;
    }

    const uint32_t period_samples = last_rising - previous_rising;
    uint32_t high_samples = 0;
    for (uint32_t index = previous_rising; index < last_rising; index++) {
        high_samples += osc_waveform_get_sample(channel, first_sample + index) >= OSC_ADC_CENTER;
    }
    const float frequency_hz = (float)s_lvgl.input_sample_rate_hz / period_samples;
    const uint32_t duty_percent = (high_samples * 100U) / period_samples;
    lv_label_set_text_fmt(s_lvgl.measurement_labels[channel][3], "FREQ %.1fHz", (double)frequency_hz);
    lv_label_set_text_fmt(s_lvgl.measurement_labels[channel][4], "DUTY %u%%", (unsigned)duty_percent);
}

/**
 * @brief Atualiza RMS, picos e medidas temporais da ultima janela desenhada.
 */
static void osc_measurement_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t sample_count = s_lvgl.last_plot_sample_count;
    const uint32_t first_sample = s_lvgl.last_plot_first_sample;
    if (sample_count == 0) {
        return;
    }

    for (uint8_t channel = 0; channel < 4; channel++) {
        if (!s_lvgl.channel_visible[channel]) {
            osc_clear_measurements(channel);
            continue;
        }
        int32_t positive_peak = 0;
        int32_t negative_peak = 0;
        uint64_t sum_squares = 0;
        for (uint32_t index = 0; index < sample_count; index++) {
            const int32_t centered = (int32_t)osc_waveform_get_sample(channel, first_sample + index) - OSC_ADC_CENTER;
            if (centered > positive_peak) {
                positive_peak = centered;
            }
            if (centered < negative_peak) {
                negative_peak = centered;
            }
            sum_squares += (uint64_t)(centered * centered);
        }

        const uint32_t rms_counts = (uint32_t)sqrt((double)sum_squares / sample_count);
        char rms_text[16] = {0};
        char positive_text[16] = {0};
        char negative_text[16] = {0};
        osc_format_voltage(osc_counts_to_mv(rms_counts), rms_text, sizeof(rms_text));
        osc_format_voltage(osc_counts_to_mv((uint32_t)positive_peak), positive_text, sizeof(positive_text));
        osc_format_voltage(osc_counts_to_mv((uint32_t)-negative_peak), negative_text, sizeof(negative_text));
        lv_label_set_text_fmt(s_lvgl.measurement_labels[channel][0], "RMS %s", rms_text);
        lv_label_set_text_fmt(s_lvgl.measurement_labels[channel][1], "PK+ %s", positive_text);
        lv_label_set_text_fmt(s_lvgl.measurement_labels[channel][2], "PK- -%s", negative_text);
        osc_update_frequency_duty(channel, first_sample, sample_count);
    }
}

/**
 * @brief Cria a area inferior com medicoes dos quatro canais.
 *
 * @param[in] parent Tela principal.
 */
static void osc_create_measurements(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, OSC_CHANNEL_INFO_WIDTH, OSC_MEASUREMENTS_HEIGHT);
    lv_obj_set_pos(container, 0, OSC_MEASUREMENTS_Y);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x606060), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(container, 4, LV_PART_SCROLLBAR);
    osc_create_measurement_row(container, 0, 0 * OSC_CHANNEL_ROW_HEIGHT);
    osc_create_measurement_row(container, 1, 1 * OSC_CHANNEL_ROW_HEIGHT);
    osc_create_measurement_row(container, 2, 2 * OSC_CHANNEL_ROW_HEIGHT);
    osc_create_measurement_row(container, 3, 3 * OSC_CHANNEL_ROW_HEIGHT);
    s_lvgl.measurement_timer = lv_timer_create(osc_measurement_timer_cb, 1000, NULL);
}

/**
 * @brief Define o painel LCD usado pelos controles do osciloscópio.
 */
void osc_set_lcd(wt32s3_lcd_handle_t lcd)
{
    s_lvgl.lcd = lcd;
}

/** @brief Define a ação executada pelo botão Menu do osciloscópio. */
void osc_set_menu_callback(osc_menu_callback_t callback)
{
    s_menu_callback = callback;
}

/** @brief Define a ação executada ao encerrar manualmente o ciclo. */
void osc_set_cycle_finished_callback(osc_cycle_finished_callback_t callback)
{
    s_cycle_finished_callback = callback;
}

/** @brief Define a ação executada para pausar ou retomar o ciclo na Power Control. */
void osc_set_cycle_pause_callback(osc_cycle_pause_callback_t callback)
{
    s_cycle_pause_callback = callback;
}

/** @brief Define a ação executada ao trocar o perfil ADC derivado da base de tempo. */
void osc_set_profile_changed_callback(osc_profile_changed_callback_t callback)
{
    s_profile_changed_callback = callback;
}

/** @brief Notifica que a STM32 concluiu uma execução PWM finita. */
void osc_notify_cycle_done(void)
{
    if (s_cycle_finished_callback != NULL) {
        s_cycle_finished_callback(true);
    }
}

/** @brief Destrói a tela do osciloscópio, seus timers e buffers de histórico. */
void osc_destroy(void)
{
    acquisition_history_set_write_paused(false);
    osc_trigger_clear_single_capture();
    if (s_lvgl.trigger_hide_timer != NULL) {
        lv_timer_delete(s_lvgl.trigger_hide_timer);
    }
    if (s_lvgl.measurement_timer != NULL) {
        lv_timer_delete(s_lvgl.measurement_timer);
    }
    if (s_lvgl.stop_cycle_blink_timer != NULL) {
        lv_timer_delete(s_lvgl.stop_cycle_blink_timer);
    }
    if (s_lvgl.screen != NULL) {
        lv_obj_delete(s_lvgl.screen);
    }
    heap_caps_free(s_lvgl.live_envelope_min);
    heap_caps_free(s_lvgl.live_envelope_max);
    heap_caps_free(s_lvgl.live_envelope_first);
    heap_caps_free(s_lvgl.live_envelope_last);
    const wt32s3_lcd_handle_t lcd = s_lvgl.lcd;
    s_lvgl = (osc_context_t){0};
    s_lvgl.lcd = lcd;
}

/**
 * @brief Cria a tela inicial do osciloscópio quando ela ainda não existe.
 *
 * @param[in] lcd Handle do painel usado pelo controle de backlight.
 * @return ESP_OK em sucesso ou ESP_ERR_NO_MEM se a tela não puder ser criada.
 */
esp_err_t osc_create(wt32s3_lcd_handle_t lcd)
{
    if (lcd != NULL) {
        s_lvgl.lcd = lcd;
    }
    ESP_RETURN_ON_FALSE(s_lvgl.lcd != NULL, ESP_ERR_INVALID_STATE, TAG, "painel LCD nao configurado");
    if (s_lvgl.screen != NULL) {
        return ESP_OK;
    }
    acquisition_history_set_write_paused(false);
    s_lvgl.screen = lv_obj_create(NULL);
    if (s_lvgl.screen == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_lvgl.theme = OSC_THEME_DARK;
    lv_obj_remove_style_all(s_lvgl.screen);
    lv_obj_clear_flag(s_lvgl.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_lvgl.screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.screen, LV_OPA_COVER, LV_PART_MAIN);

    osc_create_top_menu(s_lvgl.screen);
    osc_create_channel_buttons(s_lvgl.screen);
    osc_create_information_panels(s_lvgl.screen);
    osc_create_waveform_area(s_lvgl.screen);
    osc_create_measurements(s_lvgl.screen);
    osc_create_action_buttons(s_lvgl.screen);
    for (uint8_t i = 0; i < 4; i++) {
        if (s_lvgl.channel_buttons[i] != NULL) {
            lv_obj_move_foreground(s_lvgl.channel_buttons[i]);
        }
    }

    return ESP_OK;
}

/**
 * @brief Retorna a tela criada pelo componente do osciloscópio.
 *
 * @return Ponteiro da tela LVGL ou @c NULL se ela ainda não foi criada.
 */
lv_obj_t *osc_get_screen(void)
{
    return s_lvgl.screen;
}

/**
 * @brief Define a taxa de frames recebidos pela entrada de aquisição.
 *
 * @param[in] sample_rate_hz Taxa de frames por segundo do ADC.
 * @return @c ESP_OK em caso de sucesso ou @c ESP_ERR_INVALID_ARG se a taxa for zero.
 */
esp_err_t osc_set_input_sample_rate(uint32_t sample_rate_hz)
{
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0, ESP_ERR_INVALID_ARG, TAG, "taxa de amostragem invalida");
    if (s_lvgl.input_sample_rate_hz == sample_rate_hz) {
        return ESP_OK;
    }
    s_lvgl.input_sample_rate_hz = sample_rate_hz;
    osc_live_envelope_reset();
    if (s_lvgl.waveform_renderer != NULL) {
        lv_obj_invalidate(s_lvgl.waveform_renderer);
    }
    return ESP_OK;
}

/**
 * @brief Atualiza o snapshot visual do histórico preenchido pela task SPI.
 *
 * Esta função não percorre nem copia frames: apenas publica no renderer a
 * posição já estável do ring buffer PSRAM.
 */
void osc_refresh_acquisition_history(void)
{
    acquisition_history_snapshot_t snapshot;
    if (!acquisition_history_get_snapshot(&snapshot) || s_lvgl.waveform_renderer == NULL) {
        return;
    }
    if (snapshot.generation != s_lvgl.waveform_generation) {
        s_lvgl.waveform_generation = snapshot.generation;
        s_lvgl.history_view_offset = 0U;
        s_lvgl.history_navigation_started = false;
        osc_trigger_reset_detector();
        s_lvgl.last_plot_sample_count = 0U;
        s_lvgl.last_plot_triggered = false;
        osc_live_envelope_reset();
    }
    const bool changed = snapshot.sample_count != s_lvgl.waveform_sample_count ||
                         snapshot.sample_head != s_lvgl.waveform_sample_head ||
                         snapshot.frame_rate_hz != s_lvgl.input_sample_rate_hz;
    s_lvgl.waveform_sample_count = snapshot.sample_count;
    s_lvgl.waveform_sample_head = snapshot.sample_head;
    s_lvgl.waveform_total_frames = snapshot.total_frames;
    if (snapshot.frame_rate_hz > 0U) {
        s_lvgl.input_sample_rate_hz = snapshot.frame_rate_hz;
    }
    if (changed) {
        osc_trigger_scan_new_samples();
        osc_update_buffer_label();
        osc_request_live_waveform_render();
    }
    osc_trigger_capture_single();
}

/**
 * @brief Insere um frame ADC de quatro canais no histórico do osciloscópio.
 *
 * @param[in] samples Amostras na ordem CH1, CH2, CH3 e CH4.
 * @return @c ESP_OK em caso de sucesso ou @c ESP_ERR_INVALID_ARG se @p samples for nulo.
 */
esp_err_t osc_push_frame(const uint16_t samples[4])
{
    ESP_RETURN_ON_FALSE(samples != NULL, ESP_ERR_INVALID_ARG, TAG, "frame ADC invalido");
    if (s_lvgl.paused || s_lvgl.waveform_renderer == NULL) {
        return ESP_OK;
    }
    osc_waveform_push_samples(samples);
    osc_update_buffer_label();
    osc_request_live_waveform_render();
    return ESP_OK;
}

/**
 * @brief Insere vários frames no histórico e solicita um único redesenho.
 */
esp_err_t osc_push_frames(const uint16_t (*frames)[4], size_t frame_count)
{
    ESP_RETURN_ON_FALSE(frames != NULL || frame_count == 0, ESP_ERR_INVALID_ARG, TAG, "frames ADC invalidos");
    if (s_lvgl.paused || s_lvgl.waveform_renderer == NULL) {
        return ESP_OK;
    }
    for (size_t index = 0; index < frame_count; index++) {
        osc_waveform_push_samples(frames[index]);
    }
    if (frame_count > 0) {
        osc_update_buffer_label();
        osc_request_live_waveform_render();
    }
    return ESP_OK;
}

/**
 * @brief Retorna o perfil ADC adequado à base de tempo atualmente selecionada.
 */
uint8_t osc_get_acquisition_profile(void)
{
    if (s_lvgl.time_base_us_per_div <= 10000U) {
        return 1U;
    }
    if (s_lvgl.time_base_us_per_div <= 20000U) {
        return 2U;
    }
    return 3U;
}
