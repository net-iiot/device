#pragma once
#include <string>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// BleAlert — cliente GATT para envio de alerta ao node da mesh
//
// Faz scan pelo SERVICE_UUID do wetzel-mesh, conecta ao primeiro node
// encontrado, escreve um ButtonPacket na BUTTON_CHAR_UUID e desconecta.
//
// UUIDs alvo (wetzel-mesh):
//   Serviço:      574D0001-AABB-CCDD-8899-102030405060
//   Button char:  574D0004-AABB-CCDD-8899-102030405060
// ─────────────────────────────────────────────────────────────────────────────

namespace BleAlert
{
    struct AlertData
    {
        std::string machine_id;  // armazenado como button_id no ButtonPacket
        uint8_t     alert_type;  // 0=PRESS, 1=LONG_PRESS, 2=DOUBLE_PRESS
    };

    // Escaneia, conecta, envia alerta e desconecta.
    // Retorna true se o pacote foi escrito com sucesso.
    bool send(const AlertData &data, uint32_t timeout_ms = 20'000);
}
