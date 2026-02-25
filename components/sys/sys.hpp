#pragma once
#include "driver/gpio.h"

namespace Sys
{
    void go_deep_sleep(gpio_num_t wake_btn_pin);
    bool is_config_jumper_active(gpio_num_t jumper_pin);
}
