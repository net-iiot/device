#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include <vector>

namespace BleConfig
{
    struct ConfigData
    {
        std::string machine_id;
        std::vector<uint8_t> alert_types;  // Array de types (um por botão)
    };

    using ConfigCallback = std::function<void(ConfigData)>;
    void start(ConfigCallback cb, uint32_t timeout_ms = 30'000, std::function<bool()> should_stop = nullptr);
}
