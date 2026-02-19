#pragma once
#include <string>
#include <cstdint>

namespace Storage
{
    bool init();

    bool is_configured();

    bool save_config(const std::string &machine_id, uint8_t alert_type);

    std::string get_machine_id();
    uint8_t     get_alert_type();
}
