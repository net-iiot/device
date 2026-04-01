#pragma once
#include "driver/gpio.h"
#include <cstdint>

namespace Sys
{
    void go_deep_sleep(gpio_num_t wake_btn_pin);  // Compatibilidade
    void go_deep_sleep(uint64_t wake_pin_mask);   // Múltiplos pinos
    /** wake_pin_mask = pinos EXT1; timer_us = 0 desliga timer. Se timer_us > 0, acorda também por tempo (evita loop na 4MB). */
    void go_deep_sleep(uint64_t wake_pin_mask, uint64_t timer_us);
    bool is_config_jumper_active(gpio_num_t jumper_pin);
}
