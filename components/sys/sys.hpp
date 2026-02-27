#pragma once
#include "driver/gpio.h"

namespace Sys
{
    void go_deep_sleep(gpio_num_t wake_btn_pin);  // Compatibilidade
    void go_deep_sleep(uint64_t wake_pin_mask);   // Múltiplos pinos
    bool is_config_jumper_active(gpio_num_t jumper_pin);
}
