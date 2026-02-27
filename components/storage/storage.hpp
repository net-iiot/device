#pragma once
#include <string>
#include <cstdint>

namespace Storage
{
    bool init();

    bool is_configured();

    // Legacy - compatibilidade
    bool save_config(const std::string &machine_id, uint8_t alert_type);
    uint8_t get_alert_type();

    // Novo - múltiplos botões
    bool save_config_with_buttons(const std::string &machine_id, const uint8_t *alert_types, size_t count);
    bool save_button_alert_type(int button_id, uint8_t alert_type);
    uint8_t get_button_alert_type(int button_id);

    std::string get_machine_id();
}
