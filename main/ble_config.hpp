#pragma once
#include <string>
#include <functional>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// BleConfig — servidor GATT para configuração inicial via BLE
//
// Anuncia como "WM Alert Config" e aguarda o app escrever machine_id e
// alert_type. Após receber ambos (ou após timeout), encerra e desliga o BLE.
//
// UUIDs (mesmo prefixo 574D do wetzel-mesh, faixa 001x para configuração):
//   Serviço:     574D0010-AABB-CCDD-8899-102030405060
//   machine_id:  574D0011-AABB-CCDD-8899-102030405060  (write-no-response)
//   alert_type:  574D0012-AABB-CCDD-8899-102030405060  (write-no-response)
// ─────────────────────────────────────────────────────────────────────────────

namespace BleConfig
{
    struct ConfigData
    {
        std::string machine_id;
        uint8_t     alert_type;
    };

    using ConfigCallback = std::function<void(ConfigData)>;

    // Inicia modo configuração BLE.
    // Bloqueia por até timeout_ms ms aguardando dados do app.
    // Chama cb quando ambos os campos forem recebidos.
    // Desliga BLE ao sair.
    void start(ConfigCallback cb, uint32_t timeout_ms = 30'000, std::function<bool()> should_stop = nullptr);
}
