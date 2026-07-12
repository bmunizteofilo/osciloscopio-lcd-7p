#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia a splash gerada pelo GUI Guider e o fluxo inicial da interface.
 *
 * A splash progride de 0 a 100% em cinco segundos e, ao terminar, faz a
 * transição suave para a tela principal.
 *
 * @return @c ESP_OK em caso de sucesso ou erro ao criar a tela principal.
 */
esp_err_t ui_flow_start(void);

#ifdef __cplusplus
}
#endif
