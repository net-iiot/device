#pragma once
#include <string>
#include <functional>
#include <cstdint>

namespace BleConfig
{
    struct ConfigData
    {
        std::string machine_id;
        uint8_t     alert_type;
    };

    using ConfigCallback = std::function<void(ConfigData)>;
    void start(ConfigCallback cb, uint32_t timeout_ms = 30'000, std::function<bool()> should_stop = nullptr);
}
