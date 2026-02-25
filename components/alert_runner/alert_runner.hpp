#pragma once
#include "driver/gpio.h"

namespace AlertRunner
{
    enum class Result { Sleep, EnterConfig };
    Result run(gpio_num_t btn_pin);
}
