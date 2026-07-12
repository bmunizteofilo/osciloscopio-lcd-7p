#include "ui_flow.h"

#include <string.h>
#include "esp_check.h"
#include "gui_guider.h"
#include "gg_utils.h"

#define UI_FLOW_SPLASH_DURATION_MS 5000U
#define UI_FLOW_SPLASH_BAR_RADIUS 15

/** @brief Instância exigida pelo código exportado pelo NXP GUI Guider. */
gg_ui_t guider_ui;

/** @brief Contexto local do fluxo de telas inicial. */
typedef struct {
    lv_obj_t *splash_bar;
} ui_flow_context_t;

/** @brief Estado persistente da splash e de sua transição. */
static ui_flow_context_t s_ui_flow;

static void ui_flow_show_main_menu(void);
static void ui_flow_show_settings(void);

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
 * @brief Cria, conecta e carrega o menu principal.
 *
 * A tela anterior é removida após a troca para não manter objetos LVGL
 * ocultos alocados na memória.
 */
static void ui_flow_show_main_menu(void)
{
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
    lv_screen_load_anim(guider_ui.screen_menu_principal.screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * @brief Cria, conecta e carrega a tela de configurações.
 *
 * Os botões Voltar e Home retornam ambos ao menu principal nesta etapa.
 */
static void ui_flow_show_settings(void)
{
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
