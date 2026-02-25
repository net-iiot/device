#pragma once
#include "driver/gpio.h"
#include <cstdint>

namespace Button
{
    enum class Result
    {
        NONE,
        FIVE_CLICKS,
        HOLD_3S,
    };

    void init(gpio_num_t pin);
    Result detect();
}
