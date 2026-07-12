#include "esp_check.h"
#include "app.h"

/**
 * @brief Ponto de entrada do firmware.
 *
 * Delega a inicialização dos periféricos e serviços ao componente @c app.
 */
void app_main(void)
{
    ESP_ERROR_CHECK(app_init());
}
