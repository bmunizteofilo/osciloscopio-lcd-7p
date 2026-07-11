#include "app_lvgl.h"

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "osc.h"

/** @brief Periodo do tick entregue ao LVGL, em milissegundos. */
#define APP_LVGL_TICK_PERIOD_MS 1
/** @brief Tamanho da pilha da task responsavel pelo processamento LVGL. */
#define APP_LVGL_TASK_STACK_SIZE 8192
/** @brief Prioridade da task de processamento LVGL. */
#define APP_LVGL_TASK_PRIORITY 4
/** @brief Intervalo entre chamadas do manipulador LVGL. */
#define APP_LVGL_TASK_DELAY_MS 5

/** @brief Recursos de infraestrutura usados pela porta LVGL. */
typedef struct {
    wt32s3_lcd_handle_t lcd;
    gt911_touch_handle_t touch;
    lv_display_t *display;
    lv_indev_t *input;
    SemaphoreHandle_t lock;
    TaskHandle_t task;
} app_lvgl_context_t;

/** @brief Contexto único da infraestrutura LVGL. */
static app_lvgl_context_t s_lvgl = {0};

/** @brief Obtém exclusividade para chamadas à API LVGL. */
static void app_lvgl_lock(void)
{
    xSemaphoreTake(s_lvgl.lock, portMAX_DELAY);
}

/** @brief Libera a exclusividade das chamadas à API LVGL. */
static void app_lvgl_unlock(void)
{
    xSemaphoreGive(s_lvgl.lock);
}

/**
 * @brief Avança a base de tempo interna do LVGL.
 *
 * @param[in] arg Contexto não utilizado pelo temporizador ESP.
 */
static void app_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(APP_LVGL_TICK_PERIOD_MS);
}

/**
 * @brief Notifica a task LVGL que o painel RGB liberou o framebuffer.
 *
 * @return true quando uma task de maior prioridade foi acordada.
 */
static bool IRAM_ATTR app_lvgl_frame_complete_cb(esp_lcd_panel_handle_t panel,
                                                  const esp_lcd_rgb_panel_event_data_t *edata,
                                                  void *user_ctx)
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
 * @brief Entrega ao painel RGB o último framebuffer renderizado pelo LVGL.
 */
static void app_lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    wt32s3_lcd_handle_t lcd = (wt32s3_lcd_handle_t)lv_display_get_user_data(display);
    if (!lv_display_flush_is_last(display)) {
        lv_display_flush_ready(display);
        return;
    }
    ulTaskNotifyTake(pdTRUE, 0);
    esp_lcd_panel_draw_bitmap(wt32s3_lcd_get_panel(lcd), area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

/**
 * @brief Aguarda o painel RGB terminar o uso do framebuffer antes do próximo desenho.
 */
static void app_lvgl_flush_wait_cb(lv_display_t *display)
{
    if (lv_display_flush_is_last(display)) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    lv_display_flush_ready(display);
}

/**
 * @brief Lê o GT911 e converte seu estado para a entrada de ponteiro LVGL.
 */
static void app_lvgl_touch_read_cb(lv_indev_t *input, lv_indev_data_t *data)
{
    gt911_touch_data_t touch_data = {0};
    gt911_touch_handle_t touch = (gt911_touch_handle_t)lv_indev_get_user_data(input);
    if (gt911_touch_read(touch, &touch_data) == ESP_OK && touch_data.touched && touch_data.points > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch_data.x[0];
        data->point.y = touch_data.y[0];
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief Executa continuamente timers, animações e renderização do LVGL no core 1.
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
 * @brief Registra o display RGB e seus dois framebuffers diretos no LVGL.
 *
 * @param[in] lcd Handle do painel RGB.
 * @return ESP_OK em sucesso ou erro da camada LCD/LVGL.
 */
static esp_err_t app_lvgl_display_init(wt32s3_lcd_handle_t lcd)
{
    const size_t size = WT32S3_LCD_H_RES * WT32S3_LCD_V_RES * sizeof(lv_color_t);
    void *buffer_a = NULL;
    void *buffer_b = NULL;
    ESP_RETURN_ON_ERROR(wt32s3_lcd_get_frame_buffers(lcd, &buffer_a, &buffer_b), "app_lvgl", "framebuffers RGB indisponiveis");
    s_lvgl.display = lv_display_create(WT32S3_LCD_H_RES, WT32S3_LCD_V_RES);
    ESP_RETURN_ON_FALSE(s_lvgl.display != NULL, ESP_ERR_NO_MEM, "app_lvgl", "falha ao criar display LVGL");
    lv_display_set_color_format(s_lvgl.display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(s_lvgl.display, lcd);
    lv_display_set_flush_cb(s_lvgl.display, app_lvgl_flush_cb);
    lv_display_set_flush_wait_cb(s_lvgl.display, app_lvgl_flush_wait_cb);
    lv_display_set_buffers(s_lvgl.display, buffer_a, buffer_b, size, LV_DISPLAY_RENDER_MODE_DIRECT);
    const esp_lcd_rgb_panel_event_callbacks_t callbacks = {.on_frame_buf_complete = app_lvgl_frame_complete_cb};
    return esp_lcd_rgb_panel_register_event_callbacks(wt32s3_lcd_get_panel(lcd), &callbacks, NULL);
}

/**
 * @brief Registra o controlador GT911 como dispositivo de entrada LVGL.
 *
 * @param[in] touch Handle do controlador de toque.
 * @return ESP_OK em sucesso ou erro de memória.
 */
static esp_err_t app_lvgl_input_init(gt911_touch_handle_t touch)
{
    s_lvgl.input = lv_indev_create();
    ESP_RETURN_ON_FALSE(s_lvgl.input != NULL, ESP_ERR_NO_MEM, "app_lvgl", "falha ao criar input LVGL");
    lv_indev_set_type(s_lvgl.input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(s_lvgl.input, touch);
    lv_indev_set_read_cb(s_lvgl.input, app_lvgl_touch_read_cb);
    return ESP_OK;
}

/**
 * @brief Inicializa a infraestrutura LVGL e cria a tela do osciloscópio.
 *
 * @param[in] lcd Handle do painel RGB.
 * @param[in] touch Handle do controlador GT911.
 * @return ESP_OK em sucesso ou o primeiro erro encontrado.
 */
esp_err_t app_lvgl_init(wt32s3_lcd_handle_t lcd, gt911_touch_handle_t touch)
{
    ESP_RETURN_ON_FALSE(lcd != NULL && touch != NULL, ESP_ERR_INVALID_ARG, "app_lvgl", "handles invalidos");
    s_lvgl.lcd = lcd;
    s_lvgl.touch = touch;
    s_lvgl.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lvgl.lock != NULL, ESP_ERR_NO_MEM, "app_lvgl", "falha ao criar mutex");
    lv_init();
    ESP_RETURN_ON_ERROR(app_lvgl_display_init(lcd), "app_lvgl", "falha ao inicializar display LVGL");
    ESP_RETURN_ON_ERROR(app_lvgl_input_init(touch), "app_lvgl", "falha ao inicializar touch LVGL");
    const esp_timer_create_args_t tick_args = {.callback = app_lvgl_tick_cb, .name = "lvgl_tick"};
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), "app_lvgl", "falha ao criar tick LVGL");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, APP_LVGL_TICK_PERIOD_MS * 1000U), "app_lvgl", "falha ao iniciar tick LVGL");
    app_lvgl_lock();
    esp_err_t err = osc_create(lcd);
    app_lvgl_unlock();
    ESP_RETURN_ON_ERROR(err, "app_lvgl", "falha ao criar osciloscopio");
    BaseType_t created = xTaskCreatePinnedToCore(app_lvgl_task,
                                                 "lvgl",
                                                 APP_LVGL_TASK_STACK_SIZE,
                                                 NULL,
                                                 APP_LVGL_TASK_PRIORITY,
                                                 &s_lvgl.task,
                                                 1);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
