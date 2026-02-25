#pragma once
#include <string>
#include <cstdint>

namespace BleAlert
{
    struct AlertData
    {
        std::string machine_id;
        uint8_t     alert_type;
    };

    bool send(const AlertData &data, uint32_t timeout_ms = 20'000);
}
