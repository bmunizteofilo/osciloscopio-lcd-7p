#include "app_lvgl.h"
#include <stdbool.h>
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

#define APP_LVGL_TICK_PERIOD_MS 1
#define APP_LVGL_TASK_STACK_SIZE 8192
#define APP_LVGL_TASK_PRIORITY 4
#define APP_LVGL_TASK_DELAY_MS 5
#define APP_LVGL_SCREEN_WIDTH WT32S3_LCD_H_RES
#define APP_LVGL_SCREEN_HEIGHT WT32S3_LCD_V_RES
#define APP_LVGL_MENU_HEIGHT 56
#define APP_LVGL_MENU_LABEL_HEIGHT 16
#define APP_LVGL_MENU_DROPDOWN_HEIGHT 36
#define APP_LVGL_MENU_ITEM_WIDTH 118
#define APP_LVGL_WAVEFORM_Y 58
#define APP_LVGL_WAVEFORM_HEIGHT 368
#define APP_LVGL_WAVEFORM_BORDER 2
#define APP_LVGL_CHANNEL_BUTTON_PANEL_WIDTH 100
#define APP_LVGL_CHANNEL_BUTTON_GAP 4
#define APP_LVGL_CHANNEL_BUTTON_Y_OFFSET 3
#define APP_LVGL_CHANNEL_BUTTON_WIDTH 92
#define APP_LVGL_CHANNEL_BUTTON_HEIGHT 86
#define APP_LVGL_WAVEFORM_X APP_LVGL_CHANNEL_BUTTON_PANEL_WIDTH
#define APP_LVGL_WAVEFORM_WIDTH (APP_LVGL_SCREEN_WIDTH - APP_LVGL_WAVEFORM_X)
#define APP_LVGL_WAVEFORM_INNER_X (APP_LVGL_WAVEFORM_X + APP_LVGL_WAVEFORM_BORDER)
#define APP_LVGL_WAVEFORM_INNER_Y (APP_LVGL_WAVEFORM_Y + APP_LVGL_WAVEFORM_BORDER)
#define APP_LVGL_WAVEFORM_INNER_WIDTH (APP_LVGL_WAVEFORM_WIDTH - (APP_LVGL_WAVEFORM_BORDER * 2))
#define APP_LVGL_WAVEFORM_INNER_HEIGHT (APP_LVGL_WAVEFORM_HEIGHT - (APP_LVGL_WAVEFORM_BORDER * 2))
#define APP_LVGL_MEASUREMENTS_Y 430
#define APP_LVGL_CHANNEL_ROW_HEIGHT 25
#define APP_LVGL_CHANNEL_INFO_GAP 4
#define APP_LVGL_CHANNEL_INFO_WIDTH ((APP_LVGL_SCREEN_WIDTH - APP_LVGL_CHANNEL_INFO_GAP) / 2)
#define APP_LVGL_CHART_POINT_COUNT APP_LVGL_WAVEFORM_INNER_WIDTH
#define APP_LVGL_CHART_TIMER_MS 20
#define APP_LVGL_HEAP_LOG_PERIOD_MS 5000
#define APP_LVGL_ADC_CENTER 1024
#define APP_LVGL_ADC_DIV_VALUE 256
#define APP_LVGL_CH1_LOW (APP_LVGL_ADC_CENTER - (2 * APP_LVGL_ADC_DIV_VALUE))
#define APP_LVGL_CH1_HIGH (APP_LVGL_ADC_CENTER + (2 * APP_LVGL_ADC_DIV_VALUE))
#define APP_LVGL_CH2_LOW (APP_LVGL_ADC_CENTER - APP_LVGL_ADC_DIV_VALUE)
#define APP_LVGL_CH2_HIGH (APP_LVGL_ADC_CENTER + (3 * APP_LVGL_ADC_DIV_VALUE))
#define APP_LVGL_SQUARE_PERIOD_US 200000
#define APP_LVGL_DEFAULT_TIME_BASE_US_PER_DIV 100000
#define APP_LVGL_DEFAULT_VOLTAGE_BASE_MV_PER_DIV 50
#define APP_LVGL_TIME_DIV_COUNT 10
#define APP_LVGL_VOLTAGE_DIV_COUNT 8
#define APP_LVGL_BACKLIGHT_DEFAULT_PERCENT 80
#define APP_LVGL_BACKLIGHT_MIN_PERCENT 10
#define APP_LVGL_BACKLIGHT_MAX_PERCENT 100
#define APP_LVGL_BACKLIGHT_SLIDER_WIDTH 36
#define APP_LVGL_BACKLIGHT_SLIDER_HEIGHT ((APP_LVGL_WAVEFORM_INNER_HEIGHT * 60) / 100)
#define APP_LVGL_TRIGGER_HIDE_MS 5000
#define APP_LVGL_TRIGGER_MARKER_WIDTH 5

typedef enum {
    APP_LVGL_CURSOR_MODE_OFF = 0,
    APP_LVGL_CURSOR_MODE_TIME,
    APP_LVGL_CURSOR_MODE_VOLTAGE,
} app_lvgl_cursor_mode_t;

typedef enum {
    APP_LVGL_THEME_DARK = 0,
    APP_LVGL_THEME_LIGHT,
} app_lvgl_theme_t;

#define APP_LVGL_MONITOR_HEAP_LOG 0

static const char *TAG = "app_lvgl";

static const uint32_t APP_LVGL_CHANNEL_COLORS[] = {
    0x00c853, /* CH1 verde */
    0xffd600, /* CH2 amarelo */
    0xff1744, /* CH3 vermelho */
    0x2979ff, /* CH4 azul */
};

static const int32_t APP_LVGL_MEASUREMENT_X[] = {
    8,
    90,
    220,
    350,
    480,
    650,
};

static const uint32_t APP_LVGL_TIME_BASE_US_PER_DIV[] = {
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

static const uint32_t APP_LVGL_VOLTAGE_BASE_MV_PER_DIV[] = {
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
    lv_obj_t *grid_canvas;
    void *grid_canvas_buffer;
    lv_obj_t *channel_buttons[4];
    lv_obj_t *chart;
    lv_obj_t *cursor_lines[2];
    lv_obj_t *cursor_label;
    lv_obj_t *trigger_line;
    lv_obj_t *trigger_marker;
    lv_timer_t *trigger_hide_timer;
    lv_obj_t *backlight_slider;
    lv_obj_t *backlight_label;
    int16_t waveform_samples[2][APP_LVGL_CHART_POINT_COUNT];
    bool channel_visible[4];
    app_lvgl_cursor_mode_t cursor_mode;
    app_lvgl_theme_t theme;
    bool paused;
    bool trigger_enabled;
    uint8_t backlight_percent;
    uint32_t time_base_us_per_div;
    uint32_t voltage_base_mv_per_div;
    uint32_t waveform_head;
    uint32_t waveform_count;
    uint32_t square_phase_us;
    uint32_t chart_point_accumulator_q8;
    SemaphoreHandle_t lock;
    TaskHandle_t task;
} app_lvgl_context_t;

static app_lvgl_context_t s_lvgl = {0};

/**
 * @brief Bloqueia o acesso as APIs do LVGL.
 */
static void app_lvgl_lock(void)
{
    if (s_lvgl.lock != NULL) {
        xSemaphoreTake(s_lvgl.lock, portMAX_DELAY);
    }
}

/**
 * @brief Libera o acesso as APIs do LVGL.
 */
static void app_lvgl_unlock(void)
{
    if (s_lvgl.lock != NULL) {
        xSemaphoreGive(s_lvgl.lock);
    }
}

/**
 * @brief Atualiza o contador interno de tempo do LVGL.
 *
 * @param[in] arg Argumento nao utilizado.
 */
static void app_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(APP_LVGL_TICK_PERIOD_MS);
}

#if APP_LVGL_MONITOR_HEAP_LOG == 1
/**
 * @brief Registra periodicamente o uso de heap interno e PSRAM.
 *
 * @param[in] arg Argumento nao utilizado.
 */
static void app_lvgl_heap_log_cb(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG,
             "heap interno livre=%u maior_bloco=%u | psram livre=%u maior_bloco=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}
#endif
/**
 * @brief Notifica a task do LVGL quando o driver RGB termina de usar o framebuffer.
 *
 * @param[in] panel Handle nativo do painel RGB.
 * @param[in] edata Dados do evento, nao usados nesta porta.
 * @param[in] user_ctx Ponteiro nao utilizado.
 * @return true se uma task de maior prioridade foi acordada.
 */
static bool IRAM_ATTR app_lvgl_frame_complete_cb(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;

    BaseType_t higher_priority_task_woken = pdFALSE;
    if (s_lvgl.task != NULL) {
        vTaskNotifyGiveFromISR(s_lvgl.task, &higher_priority_task_woken);
    }
    return higher_priority_task_woken == pdTRUE;
}

/**
 * @brief Envia o framebuffer renderizado pelo LVGL para o painel RGB.
 *
 * @param[in] display Display LVGL que solicitou o flush.
 * @param[in] area Area de pixels a atualizar.
 * @param[in] px_map Buffer RGB565 renderizado pelo LVGL.
 */
static void app_lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;

    wt32s3_lcd_handle_t lcd = (wt32s3_lcd_handle_t)lv_display_get_user_data(display);
    esp_lcd_panel_handle_t panel = wt32s3_lcd_get_panel(lcd);

    if (!lv_display_flush_is_last(display)) {
        lv_display_flush_ready(display);
        return;
    }

    ulTaskNotifyTake(pdTRUE, 0);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

/**
 * @brief Aguarda o driver RGB liberar o framebuffer atual antes do proximo desenho.
 *
 * @param[in] display Display LVGL associado ao painel RGB.
 */
static void app_lvgl_flush_wait_cb(lv_display_t *display)
{
    if (lv_display_flush_is_last(display)) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    lv_display_flush_ready(display);
}

/**
 * @brief Le o estado atual do GT911 e entrega ao LVGL.
 *
 * @param[in] input Dispositivo de entrada LVGL.
 * @param[out] data Estrutura preenchida com o estado do toque.
 */
static void app_lvgl_touch_read_cb(lv_indev_t *input, lv_indev_data_t *data)
{
    gt911_touch_handle_t touch = (gt911_touch_handle_t)lv_indev_get_user_data(input);
    gt911_touch_data_t touch_data = {0};

    if (gt911_touch_read(touch, &touch_data) == ESP_OK && touch_data.touched && touch_data.points > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch_data.x[0];
        data->point.y = touch_data.y[0];
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void app_lvgl_update_cursor_label(void);
static void app_lvgl_reset_cursor_positions(void);
static void app_lvgl_draw_grid_canvas(lv_obj_t *canvas);

static uint32_t app_lvgl_theme_waveform_bg(void)
{
    return s_lvgl.theme == APP_LVGL_THEME_LIGHT ? 0xffffff : 0x000000;
}

static uint32_t app_lvgl_theme_grid_dot(void)
{
    return s_lvgl.theme == APP_LVGL_THEME_LIGHT ? 0x404040 : 0x606060;
}

static uint32_t app_lvgl_theme_grid_center(void)
{
    return s_lvgl.theme == APP_LVGL_THEME_LIGHT ? 0x000000 : 0xffffff;
}

static uint32_t app_lvgl_theme_overlay_text(void)
{
    return s_lvgl.theme == APP_LVGL_THEME_LIGHT ? 0x000000 : 0xffffff;
}

static void app_lvgl_apply_theme(void)
{
    if (s_lvgl.grid_canvas != NULL) {
        app_lvgl_draw_grid_canvas(s_lvgl.grid_canvas);
    }

    for (uint8_t i = 0; i < 2; i++) {
        if (s_lvgl.cursor_lines[i] != NULL) {
            lv_obj_set_style_bg_color(s_lvgl.cursor_lines[i], lv_color_hex(app_lvgl_theme_overlay_text()), LV_PART_MAIN);
        }
    }

    if (s_lvgl.cursor_label != NULL) {
        lv_obj_set_style_text_color(s_lvgl.cursor_label, lv_color_hex(app_lvgl_theme_overlay_text()), LV_PART_MAIN);
    }
    if (s_lvgl.backlight_label != NULL) {
        lv_obj_set_style_text_color(s_lvgl.backlight_label, lv_color_hex(app_lvgl_theme_overlay_text()), LV_PART_MAIN);
    }
}

/**
 * @brief Atualiza a base de tempo usada pelo sinal simulado.
 *
 * @param[in] event Evento LVGL do dropdown de base de tempo.
 */
static void app_lvgl_time_base_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected < (sizeof(APP_LVGL_TIME_BASE_US_PER_DIV) / sizeof(APP_LVGL_TIME_BASE_US_PER_DIV[0]))) {
        s_lvgl.time_base_us_per_div = APP_LVGL_TIME_BASE_US_PER_DIV[selected];
        s_lvgl.chart_point_accumulator_q8 = 0;
        app_lvgl_update_cursor_label();
    }
}

/**
 * @brief Atualiza a base de tensao usada pelos cursores horizontais.
 *
 * @param[in] event Evento LVGL do dropdown de base de tensao.
 */
static void app_lvgl_voltage_base_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected < (sizeof(APP_LVGL_VOLTAGE_BASE_MV_PER_DIV) / sizeof(APP_LVGL_VOLTAGE_BASE_MV_PER_DIV[0]))) {
        s_lvgl.voltage_base_mv_per_div = APP_LVGL_VOLTAGE_BASE_MV_PER_DIV[selected];
        app_lvgl_update_cursor_label();
    }
}

/**
 * @brief Atualiza o modo dos cursores.
 *
 * @param[in] event Evento LVGL do dropdown de cursores.
 */
static void app_lvgl_cursor_mode_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected <= APP_LVGL_CURSOR_MODE_VOLTAGE) {
        s_lvgl.cursor_mode = (app_lvgl_cursor_mode_t)selected;
        app_lvgl_reset_cursor_positions();
        app_lvgl_update_cursor_label();
    }
}

/**
 * @brief Atualiza o estado de execucao do sinal.
 *
 * @param[in] event Evento LVGL do dropdown Run/Pause.
 */
static void app_lvgl_run_pause_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    s_lvgl.paused = selected == 1;
    if (!s_lvgl.paused) {
        s_lvgl.cursor_mode = APP_LVGL_CURSOR_MODE_OFF;
    }
    app_lvgl_update_cursor_label();
}

/**
 * @brief Esconde visualmente a linha de trigger mantendo a area de toque ativa.
 *
 * @param[in] timer Timer LVGL do auto-hide.
 */
static void app_lvgl_trigger_hide_timer_cb(lv_timer_t *timer)
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
static void app_lvgl_show_trigger_line(void)
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
static void app_lvgl_trigger_line_event_cb(lv_event_t *event)
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
    if (y < APP_LVGL_WAVEFORM_INNER_Y) {
        y = APP_LVGL_WAVEFORM_INNER_Y;
    } else if (y > APP_LVGL_WAVEFORM_INNER_Y + APP_LVGL_WAVEFORM_INNER_HEIGHT - 2) {
        y = APP_LVGL_WAVEFORM_INNER_Y + APP_LVGL_WAVEFORM_INNER_HEIGHT - 2;
    }
    lv_obj_set_y(s_lvgl.trigger_line, y);
    app_lvgl_show_trigger_line();
}

/**
 * @brief Atualiza a habilitacao visual do nivel de trigger.
 *
 * @param[in] event Evento LVGL do dropdown Trigger.
 */
static void app_lvgl_trigger_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    s_lvgl.trigger_enabled = lv_dropdown_get_selected(dropdown) != 0;

    if (s_lvgl.trigger_line == NULL) {
        return;
    }

    if (s_lvgl.trigger_enabled) {
        app_lvgl_show_trigger_line();
    } else {
        lv_obj_set_style_bg_opa(s_lvgl.trigger_line, LV_OPA_TRANSP, LV_PART_MAIN);
        if (s_lvgl.trigger_marker != NULL) {
            lv_obj_add_flag(s_lvgl.trigger_marker, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_lvgl.trigger_hide_timer != NULL) {
            lv_timer_pause(s_lvgl.trigger_hide_timer);
        }
    }
}

/**
 * @brief Atualiza o label e o PWM do backlight.
 */
static void app_lvgl_update_backlight_value(void)
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
static void app_lvgl_backlight_slider_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    s_lvgl.backlight_percent = (uint8_t)lv_slider_get_value(slider);
    app_lvgl_update_backlight_value();
}

/**
 * @brief Mostra ou esconde o controle de brilho.
 *
 * @param[in] event Evento LVGL do dropdown Brilho.
 */
static void app_lvgl_backlight_dropdown_event_cb(lv_event_t *event)
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
static void app_lvgl_theme_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    const uint32_t selected = lv_dropdown_get_selected(dropdown);

    s_lvgl.theme = selected == 1 ? APP_LVGL_THEME_LIGHT : APP_LVGL_THEME_DARK;
    app_lvgl_apply_theme();
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
static lv_obj_t *app_lvgl_create_menu_item(lv_obj_t *parent, const char *title, const char *options, uint16_t selected, uint32_t color, lv_event_cb_t event_cb)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, APP_LVGL_MENU_ITEM_WIDTH, APP_LVGL_MENU_HEIGHT);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(item);
    lv_label_set_text(label, title);
    lv_obj_set_size(label, APP_LVGL_MENU_ITEM_WIDTH, APP_LVGL_MENU_LABEL_HEIGHT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *dropdown = lv_dropdown_create(item);
    lv_obj_set_size(dropdown, APP_LVGL_MENU_ITEM_WIDTH, APP_LVGL_MENU_DROPDOWN_HEIGHT);
    lv_obj_set_pos(dropdown, 0, APP_LVGL_MENU_LABEL_HEIGHT + 1);
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
    return item;
}

/**
 * @brief Cria o menu superior horizontal e rolavel.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_top_menu(lv_obj_t *parent)
{
    lv_obj_t *menu = lv_obj_create(parent);
    lv_obj_remove_style_all(menu);
    lv_obj_set_size(menu, APP_LVGL_SCREEN_WIDTH, APP_LVGL_MENU_HEIGHT);
    lv_obj_set_pos(menu, 0, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(menu, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(menu, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(menu, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, LV_PART_MAIN);

    app_lvgl_create_menu_item(menu, "Run", "Run\nPause", 0, 0xc62828, app_lvgl_run_pause_event_cb);
    app_lvgl_create_menu_item(menu, "Tempo", "1ms\n2ms\n5ms\n10ms\n20ms\n50ms\n100ms\n250ms\n500ms\n1s", 6, 0x455a64, app_lvgl_time_base_event_cb);
    app_lvgl_create_menu_item(menu, "Tensao", "50mV\n100mV\n200mV\n500mV\n1V\n2V\n5V\n10V", 0, 0x5d4037, app_lvgl_voltage_base_event_cb);
    app_lvgl_create_menu_item(menu, "Trigger", "Off\nNormal\nAuto\nSingle", 0, 0x6a1b9a, app_lvgl_trigger_event_cb);
    app_lvgl_create_menu_item(menu, "Borda", "Rising\nFalling", 0, 0x00838f, NULL);
    app_lvgl_create_menu_item(menu, "Trig CH", "CH1\nCH2\nCH3\nCH4", 0, 0x2e7d32, NULL);
    app_lvgl_create_menu_item(menu, "Cursores", "Off\nTempo\nTensao", 0, 0x37474f, app_lvgl_cursor_mode_event_cb);
    app_lvgl_create_menu_item(menu, "Brilho", "On\nOff", 1, 0x546e7a, app_lvgl_backlight_dropdown_event_cb);
    app_lvgl_create_menu_item(menu, "Tema", "Escuro\nClaro", 0, 0x263238, app_lvgl_theme_event_cb);
}

/**
 * @brief Gera o estado da onda quadrada e avanca o tempo do sinal.
 *
 * @param[in] elapsed_us Tempo simulado desde a ultima amostra.
 * @return true para nivel alto, false para nivel baixo.
 */
static bool app_lvgl_next_square_state(uint32_t elapsed_us)
{
    const uint32_t half_period_us = APP_LVGL_SQUARE_PERIOD_US / 2;
    const uint32_t phase = (s_lvgl.square_phase_us / half_period_us) % 2;
    s_lvgl.square_phase_us = (s_lvgl.square_phase_us + elapsed_us) % APP_LVGL_SQUARE_PERIOD_US;
    return phase == 0;
}

/**
 * @brief Atualiza o estilo visual de um botao de canal.
 *
 * @param[in] channel_index Indice do canal, de 0 a 3.
 */
static void app_lvgl_update_channel_button(uint8_t channel_index)
{
    lv_obj_t *button = s_lvgl.channel_buttons[channel_index];
    if (button == NULL) {
        return;
    }

    const bool visible = s_lvgl.channel_visible[channel_index];
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(visible ? APP_LVGL_CHANNEL_COLORS[channel_index] : 0x303030),
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
static void app_lvgl_channel_button_event_cb(lv_event_t *event)
{
    const uint8_t channel_index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (channel_index >= 4) {
        return;
    }

    s_lvgl.channel_visible[channel_index] = !s_lvgl.channel_visible[channel_index];
    app_lvgl_update_channel_button(channel_index);
    if (s_lvgl.chart != NULL) {
        lv_obj_invalidate(s_lvgl.chart);
    }
}

/**
 * @brief Cria os botoes laterais de habilitacao dos canais.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_channel_buttons(lv_obj_t *parent)
{
    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t *button = lv_button_create(parent);
        s_lvgl.channel_buttons[i] = button;
        s_lvgl.channel_visible[i] = i < 2;
        lv_obj_set_size(button, APP_LVGL_CHANNEL_BUTTON_WIDTH, APP_LVGL_CHANNEL_BUTTON_HEIGHT);
        lv_obj_set_pos(button,
                       APP_LVGL_CHANNEL_BUTTON_GAP,
                       APP_LVGL_WAVEFORM_Y + APP_LVGL_CHANNEL_BUTTON_GAP + APP_LVGL_CHANNEL_BUTTON_Y_OFFSET + (i * (APP_LVGL_CHANNEL_BUTTON_HEIGHT + APP_LVGL_CHANNEL_BUTTON_GAP)));
        lv_obj_set_style_radius(button, 7, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_add_event_cb(button, app_lvgl_channel_button_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text_fmt(label, "CH%u", (unsigned)(i + 1));
        lv_obj_center(label);
        app_lvgl_update_channel_button(i);
    }
}

/**
 * @brief Formata um intervalo de tempo em uma label compacta.
 *
 * @param[in] delta_us Intervalo em microssegundos.
 * @param[out] buffer Buffer de texto.
 * @param[in] buffer_size Tamanho do buffer.
 */
static void app_lvgl_format_time(uint32_t delta_us, char *buffer, size_t buffer_size)
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
static void app_lvgl_format_frequency(uint32_t delta_us, char *buffer, size_t buffer_size)
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
static void app_lvgl_format_voltage(uint32_t delta_mv, char *buffer, size_t buffer_size)
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
static void app_lvgl_reset_cursor_positions(void)
{
    if (s_lvgl.cursor_lines[0] == NULL || s_lvgl.cursor_lines[1] == NULL) {
        return;
    }

    if (s_lvgl.cursor_mode == APP_LVGL_CURSOR_MODE_TIME) {
        lv_obj_set_pos(s_lvgl.cursor_lines[0],
                       APP_LVGL_WAVEFORM_INNER_X + (APP_LVGL_WAVEFORM_INNER_WIDTH / 3),
                       APP_LVGL_WAVEFORM_INNER_Y);
        lv_obj_set_pos(s_lvgl.cursor_lines[1],
                       APP_LVGL_WAVEFORM_INNER_X + ((APP_LVGL_WAVEFORM_INNER_WIDTH * 2) / 3),
                       APP_LVGL_WAVEFORM_INNER_Y);
    } else if (s_lvgl.cursor_mode == APP_LVGL_CURSOR_MODE_VOLTAGE) {
        lv_obj_set_pos(s_lvgl.cursor_lines[0],
                       APP_LVGL_WAVEFORM_INNER_X,
                       APP_LVGL_WAVEFORM_INNER_Y + (APP_LVGL_WAVEFORM_INNER_HEIGHT / 3));
        lv_obj_set_pos(s_lvgl.cursor_lines[1],
                       APP_LVGL_WAVEFORM_INNER_X,
                       APP_LVGL_WAVEFORM_INNER_Y + ((APP_LVGL_WAVEFORM_INNER_HEIGHT * 2) / 3));
    }
}

/**
 * @brief Atualiza visibilidade, posicao e texto dos cursores.
 */
static void app_lvgl_update_cursor_label(void)
{
    if (s_lvgl.cursor_lines[0] == NULL || s_lvgl.cursor_lines[1] == NULL || s_lvgl.cursor_label == NULL) {
        return;
    }

    const bool time_mode = s_lvgl.paused && s_lvgl.cursor_mode == APP_LVGL_CURSOR_MODE_TIME;
    const bool voltage_mode = s_lvgl.paused && s_lvgl.cursor_mode == APP_LVGL_CURSOR_MODE_VOLTAGE;
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
            lv_obj_set_size(s_lvgl.cursor_lines[i], 2, APP_LVGL_WAVEFORM_INNER_HEIGHT);
            lv_obj_set_y(s_lvgl.cursor_lines[i], APP_LVGL_WAVEFORM_INNER_Y);
        }

        const int32_t x0 = lv_obj_get_x(s_lvgl.cursor_lines[0]) - APP_LVGL_WAVEFORM_INNER_X;
        const int32_t x1 = lv_obj_get_x(s_lvgl.cursor_lines[1]) - APP_LVGL_WAVEFORM_INNER_X;
        const uint32_t dx = (uint32_t)LV_ABS(x1 - x0);
        const uint32_t delta_us = (dx * s_lvgl.time_base_us_per_div * APP_LVGL_TIME_DIV_COUNT) / APP_LVGL_WAVEFORM_INNER_WIDTH;
        char time_text[24] = {0};
        char freq_text[24] = {0};
        app_lvgl_format_time(delta_us, time_text, sizeof(time_text));
        app_lvgl_format_frequency(delta_us, freq_text, sizeof(freq_text));
        lv_label_set_text_fmt(s_lvgl.cursor_label, "dT %s  F %s", time_text, freq_text);
    } else if (voltage_mode) {
        for (uint8_t i = 0; i < 2; i++) {
            lv_obj_set_size(s_lvgl.cursor_lines[i], APP_LVGL_WAVEFORM_INNER_WIDTH, 2);
            lv_obj_set_x(s_lvgl.cursor_lines[i], APP_LVGL_WAVEFORM_INNER_X);
        }

        const int32_t y0 = lv_obj_get_y(s_lvgl.cursor_lines[0]) - APP_LVGL_WAVEFORM_INNER_Y;
        const int32_t y1 = lv_obj_get_y(s_lvgl.cursor_lines[1]) - APP_LVGL_WAVEFORM_INNER_Y;
        const uint32_t dy = (uint32_t)LV_ABS(y1 - y0);
        const uint32_t delta_mv = (dy * s_lvgl.voltage_base_mv_per_div * APP_LVGL_VOLTAGE_DIV_COUNT) / APP_LVGL_WAVEFORM_INNER_HEIGHT;
        char voltage_text[24] = {0};
        app_lvgl_format_voltage(delta_mv, voltage_text, sizeof(voltage_text));
        lv_label_set_text_fmt(s_lvgl.cursor_label, "dV %s", voltage_text);
    }

    if (s_lvgl.grid_canvas != NULL) {
        lv_obj_align_to(s_lvgl.cursor_label, s_lvgl.grid_canvas, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
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
static void app_lvgl_cursor_line_event_cb(lv_event_t *event)
{
    const uint8_t cursor_index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (cursor_index >= 2 || !s_lvgl.paused || s_lvgl.cursor_mode == APP_LVGL_CURSOR_MODE_OFF) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_point_t point = {0};
    lv_indev_get_point(indev, &point);
    lv_obj_t *line = s_lvgl.cursor_lines[cursor_index];

    if (s_lvgl.cursor_mode == APP_LVGL_CURSOR_MODE_TIME) {
        int32_t x = point.x;
        if (x < APP_LVGL_WAVEFORM_INNER_X) {
            x = APP_LVGL_WAVEFORM_INNER_X;
        } else if (x > APP_LVGL_WAVEFORM_INNER_X + APP_LVGL_WAVEFORM_INNER_WIDTH - 2) {
            x = APP_LVGL_WAVEFORM_INNER_X + APP_LVGL_WAVEFORM_INNER_WIDTH - 2;
        }
        lv_obj_set_x(line, x);
    } else {
        int32_t y = point.y;
        if (y < APP_LVGL_WAVEFORM_INNER_Y) {
            y = APP_LVGL_WAVEFORM_INNER_Y;
        } else if (y > APP_LVGL_WAVEFORM_INNER_Y + APP_LVGL_WAVEFORM_INNER_HEIGHT - 2) {
            y = APP_LVGL_WAVEFORM_INNER_Y + APP_LVGL_WAVEFORM_INNER_HEIGHT - 2;
        }
        lv_obj_set_y(line, y);
    }

    app_lvgl_update_cursor_label();
}

/**
 * @brief Cria os cursores de tempo/tensao sobre a area da waveform.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_cursors(lv_obj_t *parent)
{
    const int32_t initial_x[] = {
        APP_LVGL_WAVEFORM_INNER_X + (APP_LVGL_WAVEFORM_INNER_WIDTH / 3),
        APP_LVGL_WAVEFORM_INNER_X + ((APP_LVGL_WAVEFORM_INNER_WIDTH * 2) / 3),
    };
    const int32_t initial_y[] = {
        APP_LVGL_WAVEFORM_INNER_Y + (APP_LVGL_WAVEFORM_INNER_HEIGHT / 3),
        APP_LVGL_WAVEFORM_INNER_Y + ((APP_LVGL_WAVEFORM_INNER_HEIGHT * 2) / 3),
    };

    for (uint8_t i = 0; i < 2; i++) {
        lv_obj_t *line = lv_obj_create(parent);
        s_lvgl.cursor_lines[i] = line;
        lv_obj_remove_style_all(line);
        lv_obj_set_pos(line, initial_x[i], initial_y[i]);
        lv_obj_set_size(line, 2, APP_LVGL_WAVEFORM_INNER_HEIGHT);
        lv_obj_set_style_bg_color(line, lv_color_hex(app_lvgl_theme_overlay_text()), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(line, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(line, 18);
        lv_obj_add_event_cb(line, app_lvgl_cursor_line_event_cb, LV_EVENT_PRESSING, (void *)(uintptr_t)i);
        lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    }

    s_lvgl.cursor_label = lv_label_create(parent);
    lv_obj_set_width(s_lvgl.cursor_label, 240);
    lv_obj_set_style_text_color(s_lvgl.cursor_label, lv_color_hex(app_lvgl_theme_overlay_text()), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lvgl.cursor_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.cursor_label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lvgl.cursor_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(s_lvgl.cursor_label, "");
    if (s_lvgl.grid_canvas != NULL) {
        lv_obj_align_to(s_lvgl.cursor_label, s_lvgl.grid_canvas, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    }
    lv_obj_add_flag(s_lvgl.cursor_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Cria a linha de nivel de trigger.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_trigger_line(lv_obj_t *parent)
{
    s_lvgl.trigger_enabled = false;

    s_lvgl.trigger_line = lv_obj_create(parent);
    lv_obj_remove_style_all(s_lvgl.trigger_line);
    lv_obj_set_size(s_lvgl.trigger_line, APP_LVGL_WAVEFORM_INNER_WIDTH, 2);
    lv_obj_set_pos(s_lvgl.trigger_line,
                   APP_LVGL_WAVEFORM_INNER_X,
                   APP_LVGL_WAVEFORM_INNER_Y + (APP_LVGL_WAVEFORM_INNER_HEIGHT / 2));
    lv_obj_set_style_bg_color(s_lvgl.trigger_line, lv_color_hex(0xffd600), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.trigger_line, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_lvgl.trigger_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_lvgl.trigger_line, 18);
    lv_obj_add_event_cb(s_lvgl.trigger_line, app_lvgl_trigger_line_event_cb, LV_EVENT_PRESSING, NULL);

    s_lvgl.trigger_marker = lv_obj_create(parent);
    lv_obj_remove_style_all(s_lvgl.trigger_marker);
    lv_obj_set_size(s_lvgl.trigger_marker, APP_LVGL_TRIGGER_MARKER_WIDTH, 2);
    lv_obj_set_pos(s_lvgl.trigger_marker,
                   APP_LVGL_WAVEFORM_INNER_X,
                   APP_LVGL_WAVEFORM_INNER_Y + (APP_LVGL_WAVEFORM_INNER_HEIGHT / 2));
    lv_obj_set_style_bg_color(s_lvgl.trigger_marker, lv_color_hex(0xffd600), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.trigger_marker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_lvgl.trigger_marker, LV_OBJ_FLAG_HIDDEN);

    s_lvgl.trigger_hide_timer = lv_timer_create(app_lvgl_trigger_hide_timer_cb, APP_LVGL_TRIGGER_HIDE_MS, NULL);
    if (s_lvgl.trigger_hide_timer != NULL) {
        lv_timer_pause(s_lvgl.trigger_hide_timer);
    }
}

/**
 * @brief Cria o controle vertical de brilho dentro da waveform.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_backlight_control(lv_obj_t *parent)
{
    s_lvgl.backlight_percent = APP_LVGL_BACKLIGHT_DEFAULT_PERCENT;

    s_lvgl.backlight_slider = lv_slider_create(parent);
    lv_obj_set_size(s_lvgl.backlight_slider, APP_LVGL_BACKLIGHT_SLIDER_WIDTH, APP_LVGL_BACKLIGHT_SLIDER_HEIGHT);
    lv_obj_set_pos(s_lvgl.backlight_slider,
                   APP_LVGL_WAVEFORM_INNER_X + ((APP_LVGL_WAVEFORM_INNER_WIDTH - APP_LVGL_BACKLIGHT_SLIDER_WIDTH) / 2),
                   APP_LVGL_WAVEFORM_INNER_Y + ((APP_LVGL_WAVEFORM_INNER_HEIGHT - APP_LVGL_BACKLIGHT_SLIDER_HEIGHT) / 2));
    lv_slider_set_range(s_lvgl.backlight_slider, APP_LVGL_BACKLIGHT_MIN_PERCENT, APP_LVGL_BACKLIGHT_MAX_PERCENT);
    lv_slider_set_value(s_lvgl.backlight_slider, s_lvgl.backlight_percent, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_lvgl.backlight_slider, 18, LV_PART_MAIN);
    lv_obj_set_style_radius(s_lvgl.backlight_slider, 18, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_lvgl.backlight_slider, 18, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s_lvgl.backlight_slider, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_lvgl.backlight_slider, lv_color_hex(0x4fc3f7), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_lvgl.backlight_slider, lv_color_hex(0x4fc3f7), LV_PART_KNOB);
    lv_obj_add_event_cb(s_lvgl.backlight_slider, app_lvgl_backlight_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_flag(s_lvgl.backlight_slider, LV_OBJ_FLAG_HIDDEN);

    s_lvgl.backlight_label = lv_label_create(parent);
    lv_obj_set_style_text_color(s_lvgl.backlight_label, lv_color_hex(app_lvgl_theme_overlay_text()), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lvgl.backlight_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_fmt(s_lvgl.backlight_label, "%u%%", (unsigned)s_lvgl.backlight_percent);
    lv_obj_align_to(s_lvgl.backlight_label, s_lvgl.backlight_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    lv_obj_add_flag(s_lvgl.backlight_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Calcula quantos pontos do chart devem ser avancados nesta atualizacao LVGL.
 *
 * @return Quantidade de pontos a inserir no chart.
 */
static uint32_t app_lvgl_chart_points_per_tick(void)
{
    const uint32_t screen_time_us = s_lvgl.time_base_us_per_div * APP_LVGL_TIME_DIV_COUNT;
    if (screen_time_us == 0) {
        return 1;
    }

    const uint32_t points_q8 = (APP_LVGL_CHART_TIMER_MS * 1000U * APP_LVGL_CHART_POINT_COUNT * 256U) / screen_time_us;
    s_lvgl.chart_point_accumulator_q8 += points_q8;

    uint32_t points = s_lvgl.chart_point_accumulator_q8 / 256U;
    s_lvgl.chart_point_accumulator_q8 %= 256U;
    if (points == 0) {
        points = 1;
    } else if (points > APP_LVGL_CHART_POINT_COUNT) {
        points = APP_LVGL_CHART_POINT_COUNT;
    }

    return points;
}

/**
 * @brief Atualiza o chart: primeiro preenche da esquerda para direita, depois desloca como esteira.
 *
 * @param[in] timer Timer LVGL que controla a simulacao.
 */
static void app_lvgl_chart_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_lvgl.chart == NULL) {
        return;
    }
    if (s_lvgl.paused) {
        return;
    }

    const uint32_t points_to_add = app_lvgl_chart_points_per_tick();
    const uint32_t sample_time_us = (s_lvgl.time_base_us_per_div * APP_LVGL_TIME_DIV_COUNT) / APP_LVGL_CHART_POINT_COUNT;

    for (uint32_t i = 0; i < points_to_add; i++) {
        const bool high = app_lvgl_next_square_state(sample_time_us);
        const int32_t ch1_sample = high ? APP_LVGL_CH1_HIGH : APP_LVGL_CH1_LOW;
        const int32_t ch2_sample = high ? APP_LVGL_CH2_HIGH : APP_LVGL_CH2_LOW;
        const uint32_t write_index = (s_lvgl.waveform_head + s_lvgl.waveform_count) % APP_LVGL_CHART_POINT_COUNT;
        s_lvgl.waveform_samples[0][write_index] = (int16_t)ch1_sample;
        s_lvgl.waveform_samples[1][write_index] = (int16_t)ch2_sample;

        if (s_lvgl.waveform_count < APP_LVGL_CHART_POINT_COUNT) {
            s_lvgl.waveform_count++;
        } else {
            s_lvgl.waveform_head = (s_lvgl.waveform_head + 1U) % APP_LVGL_CHART_POINT_COUNT;
        }
    }

    lv_obj_invalidate(s_lvgl.chart);
}

/**
 * @brief Desenha os dois canais a partir de um ring buffer, sem deslocar amostras.
 *
 * O indice mais antigo ocupa a coluna esquerda. Ao sobrescrever o ring buffer, o
 * indice inicial avanca uma coluna e mantem o efeito visual de esteira.
 */
static void app_lvgl_waveform_draw_event_cb(lv_event_t *event)
{
    if (s_lvgl.waveform_count < 2) {
        return;
    }

    lv_obj_t *obj = lv_event_get_target_obj(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.raw_end = 1;

    const int32_t height = lv_area_get_height(&coords);
    const uint32_t value_range = APP_LVGL_ADC_CENTER * 2U;
    const uint32_t point_count = s_lvgl.waveform_count;

    for (uint8_t channel = 0; channel < 2; channel++) {
        if (!s_lvgl.channel_visible[channel]) {
            continue;
        }

        line_dsc.color = lv_color_hex(APP_LVGL_CHANNEL_COLORS[channel]);
        for (uint32_t x = 1; x < point_count; x++) {
            const uint32_t previous_index = (s_lvgl.waveform_head + x - 1U) % APP_LVGL_CHART_POINT_COUNT;
            const uint32_t current_index = (s_lvgl.waveform_head + x) % APP_LVGL_CHART_POINT_COUNT;
            const int32_t previous_value = s_lvgl.waveform_samples[channel][previous_index];
            const int32_t current_value = s_lvgl.waveform_samples[channel][current_index];

            line_dsc.p1.x = coords.x1 + (int32_t)x - 1;
            line_dsc.p2.x = coords.x1 + (int32_t)x;
            line_dsc.p1.y = coords.y1 + ((int32_t)(value_range - previous_value) * (height - 1) / (int32_t)value_range);
            line_dsc.p2.y = coords.y1 + ((int32_t)(value_range - current_value) * (height - 1) / (int32_t)value_range);
            lv_draw_line(lv_event_get_layer(event), &line_dsc);
        }
    }
}

/**
 * @brief Cria o chart transparente para desenhar o sinal simulado.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_waveform_chart(lv_obj_t *parent)
{
    s_lvgl.chart = lv_obj_create(parent);
    lv_obj_remove_style_all(s_lvgl.chart);
    lv_obj_set_size(s_lvgl.chart, APP_LVGL_WAVEFORM_INNER_WIDTH, APP_LVGL_WAVEFORM_INNER_HEIGHT);
    lv_obj_set_pos(s_lvgl.chart, APP_LVGL_WAVEFORM_INNER_X, APP_LVGL_WAVEFORM_INNER_Y);
    lv_obj_set_style_bg_opa(s_lvgl.chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_lvgl.chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_lvgl.chart, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_lvgl.chart, app_lvgl_waveform_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

    s_lvgl.waveform_head = 0;
    s_lvgl.waveform_count = 0;
    s_lvgl.time_base_us_per_div = APP_LVGL_DEFAULT_TIME_BASE_US_PER_DIV;
    s_lvgl.voltage_base_mv_per_div = APP_LVGL_DEFAULT_VOLTAGE_BASE_MV_PER_DIV;
    s_lvgl.square_phase_us = 0;
    s_lvgl.chart_point_accumulator_q8 = 0;
    s_lvgl.paused = false;
    lv_timer_create(app_lvgl_chart_timer_cb, APP_LVGL_CHART_TIMER_MS, NULL);
}

/**
 * @brief Desenha a grade pontilhada em um canvas unico.
 *
 * @param[in] canvas Canvas da area util da waveform.
 */
static void app_lvgl_draw_grid_canvas(lv_obj_t *canvas)
{
    const int32_t width = APP_LVGL_WAVEFORM_INNER_WIDTH;
    const int32_t height = APP_LVGL_WAVEFORM_INNER_HEIGHT;
    const int32_t center_x = width / 2;
    const int32_t center_y = height / 2;

    lv_canvas_fill_bg(canvas, lv_color_hex(app_lvgl_theme_waveform_bg()), LV_OPA_COVER);

    for (int32_t horizontal_index = 1; horizontal_index < 8; horizontal_index++) {
        if (horizontal_index == 4) {
            continue;
        }
        const int32_t y = horizontal_index * 46;
        if (y < 0 || y >= height) {
            continue;
        }
        for (int32_t x = 0; x < width; x += 8) {
            lv_canvas_set_px(canvas, x, y, lv_color_hex(app_lvgl_theme_grid_dot()), LV_OPA_COVER);
        }
    }

    for (int32_t vertical_index = 1; vertical_index < 10; vertical_index++) {
        if (vertical_index == 5) {
            continue;
        }
        const int32_t x = vertical_index * 70;
        if (x < 0 || x >= width) {
            continue;
        }
        for (int32_t y = 0; y < height; y += 8) {
            lv_canvas_set_px(canvas, x, y, lv_color_hex(app_lvgl_theme_grid_dot()), LV_OPA_COVER);
        }
    }

    for (int32_t x = 0; x < width; x += 4) {
        lv_canvas_set_px(canvas, x, center_y, lv_color_hex(app_lvgl_theme_grid_center()), LV_OPA_COVER);
    }

    for (int32_t y = 0; y < height; y += 4) {
        lv_canvas_set_px(canvas, center_x, y, lv_color_hex(app_lvgl_theme_grid_center()), LV_OPA_COVER);
    }
}

/**
 * @brief Cria o canvas da grade da area util.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_grid_canvas(lv_obj_t *parent)
{
    const size_t canvas_size = APP_LVGL_WAVEFORM_INNER_WIDTH * APP_LVGL_WAVEFORM_INNER_HEIGHT * sizeof(lv_color_t);
    s_lvgl.grid_canvas_buffer = heap_caps_malloc(canvas_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lvgl.grid_canvas_buffer == NULL) {
        ESP_LOGE(TAG, "falha ao alocar canvas da grade (%u bytes)", (unsigned)canvas_size);
        return;
    }

    s_lvgl.grid_canvas = lv_canvas_create(parent);
    lv_obj_remove_style_all(s_lvgl.grid_canvas);
    lv_obj_set_pos(s_lvgl.grid_canvas, APP_LVGL_WAVEFORM_INNER_X, APP_LVGL_WAVEFORM_INNER_Y);
    lv_canvas_set_buffer(s_lvgl.grid_canvas,
                         s_lvgl.grid_canvas_buffer,
                         APP_LVGL_WAVEFORM_INNER_WIDTH,
                         APP_LVGL_WAVEFORM_INNER_HEIGHT,
                         LV_COLOR_FORMAT_RGB565);
    app_lvgl_draw_grid_canvas(s_lvgl.grid_canvas);
}

/**
 * @brief Cria a area de waveform com borda e grade pontilhada.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_waveform_area(lv_obj_t *parent)
{
    lv_obj_t *waveform = lv_obj_create(parent);
    lv_obj_remove_style_all(waveform);
    lv_obj_set_size(waveform, APP_LVGL_WAVEFORM_WIDTH, APP_LVGL_WAVEFORM_HEIGHT);
    lv_obj_set_pos(waveform, APP_LVGL_WAVEFORM_X, APP_LVGL_WAVEFORM_Y);
    lv_obj_set_style_bg_color(waveform, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(waveform, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(waveform, APP_LVGL_WAVEFORM_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_color(waveform, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_radius(waveform, 0, LV_PART_MAIN);

    app_lvgl_create_grid_canvas(parent);
    app_lvgl_create_waveform_chart(parent);
    app_lvgl_create_cursors(parent);
    app_lvgl_create_trigger_line(parent);
    app_lvgl_create_backlight_control(parent);
}

/**
 * @brief Cria uma linha de medicoes de canal.
 *
 * @param[in] parent Tela principal.
 * @param[in] channel_index Indice do canal, de 0 a 3.
 * @param[in] column Coluna da linha inferior, de 0 a 1.
 * @param[in] y Coordenada vertical da linha.
 */
static void app_lvgl_create_measurement_row(lv_obj_t *parent, uint8_t channel_index, uint8_t column, int32_t y)
{
    const char *texts[] = {
        "CH%u",
        "RMS 0.00",
        "PK+ 0.00",
        "PK- 0.00",
        "FREQ 0.0 Hz",
        "DUTY 0%",
    };
    const lv_color_t text_color = lv_color_hex(channel_index == 1 ? 0x000000 : 0xffffff);

    const int32_t x = column == 0 ? 0 : APP_LVGL_CHANNEL_INFO_WIDTH + APP_LVGL_CHANNEL_INFO_GAP;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, APP_LVGL_CHANNEL_INFO_WIDTH, APP_LVGL_CHANNEL_ROW_HEIGHT);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_LVGL_CHANNEL_COLORS[channel_index]), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);

    for (size_t i = 0; i < (sizeof(texts) / sizeof(texts[0])); i++) {
        lv_obj_t *label = lv_label_create(row);
        if (i == 0) {
            lv_label_set_text_fmt(label, texts[i], (unsigned)(channel_index + 1));
        } else {
            lv_label_set_text(label, texts[i]);
        }
        lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, APP_LVGL_MEASUREMENT_X[i], 0);
    }
}

/**
 * @brief Cria a area inferior com medicoes dos quatro canais.
 *
 * @param[in] parent Tela principal.
 */
static void app_lvgl_create_measurements(lv_obj_t *parent)
{
    app_lvgl_create_measurement_row(parent, 0, 0, APP_LVGL_MEASUREMENTS_Y);
    app_lvgl_create_measurement_row(parent, 2, 1, APP_LVGL_MEASUREMENTS_Y);
    app_lvgl_create_measurement_row(parent, 1, 0, APP_LVGL_MEASUREMENTS_Y + APP_LVGL_CHANNEL_ROW_HEIGHT);
    app_lvgl_create_measurement_row(parent, 3, 1, APP_LVGL_MEASUREMENTS_Y + APP_LVGL_CHANNEL_ROW_HEIGHT);
}

/**
 * @brief Cria a tela inicial do osciloscopio.
 */
static void app_lvgl_create_ui(void)
{
    s_lvgl.screen = lv_obj_create(NULL);
    s_lvgl.theme = APP_LVGL_THEME_DARK;
    lv_obj_remove_style_all(s_lvgl.screen);
    lv_obj_set_style_bg_color(s_lvgl.screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lvgl.screen, LV_OPA_COVER, LV_PART_MAIN);

    app_lvgl_create_top_menu(s_lvgl.screen);
    app_lvgl_create_channel_buttons(s_lvgl.screen);
    app_lvgl_create_waveform_area(s_lvgl.screen);
    app_lvgl_create_measurements(s_lvgl.screen);
    for (uint8_t i = 0; i < 4; i++) {
        if (s_lvgl.channel_buttons[i] != NULL) {
            lv_obj_move_foreground(s_lvgl.channel_buttons[i]);
        }
    }

    lv_screen_load(s_lvgl.screen);
}

/**
 * @brief Executa o loop principal do LVGL.
 *
 * @param[in] arg Argumento nao utilizado.
 */
static void app_lvgl_task(void *arg)
{
    (void)arg;

    while (true) {
        app_lvgl_lock();
        lv_timer_handler();
        app_lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(APP_LVGL_TASK_DELAY_MS));
    }
}

/**
 * @brief Inicializa os buffers de desenho e registra o display no LVGL.
 *
 * @param[in] lcd Handle do driver do LCD RGB.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
static esp_err_t app_lvgl_display_init(wt32s3_lcd_handle_t lcd)
{
    size_t draw_buffer_size = WT32S3_LCD_H_RES * WT32S3_LCD_V_RES * sizeof(lv_color_t);
    void *draw_buffer_a = NULL;
    void *draw_buffer_b = NULL;
    ESP_RETURN_ON_ERROR(wt32s3_lcd_get_frame_buffers(lcd, &draw_buffer_a, &draw_buffer_b), TAG, "falha ao obter framebuffers RGB");

    s_lvgl.display = lv_display_create(WT32S3_LCD_H_RES, WT32S3_LCD_V_RES);
    ESP_RETURN_ON_FALSE(s_lvgl.display != NULL, ESP_ERR_NO_MEM, TAG, "falha ao criar display LVGL");

    lv_display_set_color_format(s_lvgl.display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(s_lvgl.display, lcd);
    lv_display_set_flush_cb(s_lvgl.display, app_lvgl_flush_cb);
    lv_display_set_flush_wait_cb(s_lvgl.display, app_lvgl_flush_wait_cb);
    lv_display_set_buffers(s_lvgl.display, draw_buffer_a, draw_buffer_b, draw_buffer_size, LV_DISPLAY_RENDER_MODE_DIRECT);

    const esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_frame_buf_complete = app_lvgl_frame_complete_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_register_event_callbacks(wt32s3_lcd_get_panel(lcd), &callbacks, NULL), TAG, "falha ao registrar callback RGB");
    return ESP_OK;
}

/**
 * @brief Registra o controlador de toque como entrada de ponteiro no LVGL.
 *
 * @param[in] touch Handle do driver GT911.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
static esp_err_t app_lvgl_input_init(gt911_touch_handle_t touch)
{
    s_lvgl.input = lv_indev_create();
    ESP_RETURN_ON_FALSE(s_lvgl.input != NULL, ESP_ERR_NO_MEM, TAG, "falha ao criar input LVGL");

    lv_indev_set_type(s_lvgl.input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(s_lvgl.input, touch);
    lv_indev_set_read_cb(s_lvgl.input, app_lvgl_touch_read_cb);
    return ESP_OK;
}

/**
 * @brief Inicializa o LVGL, registra display, touch e cria a tela inicial.
 *
 * @param[in] lcd Handle do driver do LCD RGB.
 * @param[in] touch Handle do driver de touch GT911.
 * @return ESP_OK em caso de sucesso ou codigo de erro do ESP-IDF.
 */
esp_err_t app_lvgl_init(wt32s3_lcd_handle_t lcd, gt911_touch_handle_t touch)
{
    ESP_RETURN_ON_FALSE(lcd != NULL && touch != NULL, ESP_ERR_INVALID_ARG, TAG, "handles invalidos");

    s_lvgl.lcd = lcd;
    s_lvgl.touch = touch;
    s_lvgl.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lvgl.lock != NULL, ESP_ERR_NO_MEM, TAG, "falha ao criar mutex");

    lv_init();
    ESP_RETURN_ON_ERROR(app_lvgl_display_init(lcd), TAG, "falha ao inicializar display LVGL");
    ESP_RETURN_ON_ERROR(app_lvgl_input_init(touch), TAG, "falha ao inicializar touch LVGL");

    const esp_timer_create_args_t tick_timer_args = {
        .callback = app_lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &tick_timer), TAG, "falha ao criar timer LVGL");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, APP_LVGL_TICK_PERIOD_MS * 1000), TAG, "falha ao iniciar timer LVGL");

#if APP_LVGL_MONITOR_HEAP_LOG == 1
    const esp_timer_create_args_t heap_timer_args = {
        .callback = app_lvgl_heap_log_cb,
        .name = "lvgl_heap_log",
    };
    esp_timer_handle_t heap_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&heap_timer_args, &heap_timer), TAG, "falha ao criar timer de heap");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(heap_timer, APP_LVGL_HEAP_LOG_PERIOD_MS * 1000), TAG, "falha ao iniciar timer de heap");
#endif

    app_lvgl_lock();
    app_lvgl_create_ui();
    app_lvgl_unlock();

    BaseType_t task_created = xTaskCreate(app_lvgl_task, "lvgl", APP_LVGL_TASK_STACK_SIZE, NULL, APP_LVGL_TASK_PRIORITY, &s_lvgl.task);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "falha ao criar task LVGL");
    return ESP_OK;
}
