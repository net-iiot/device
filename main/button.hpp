#pragma once
#include "driver/gpio.h"
#include <cstdint>

namespace Button
{
    enum class Result
    {
        NONE,        // sem ação reconhecida — volta a dormir
        FIVE_CLICKS, // 5 cliques rápidos → modo configuração
        HOLD_3S,     // segurou 3 segundos → disparar alerta
    };

    // Configura GPIO e ISR. Deve ser chamado logo após acordar do deep sleep.
    void init(gpio_num_t pin);

    // Fica ativo por até DETECTION_WINDOW_MS ms detectando padrão do botão.
    // Retorna o resultado assim que identificado, ou NONE após timeout.
    Result detect();
}
