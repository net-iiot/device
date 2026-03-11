# device-alert

Firmware ESP32 para dispositivo de alerta por botão. Acorda por GPIO, envia evento via BLE para um node da mesh e volta a deep sleep.

## Requisitos

- ESP-IDF 5.x
- Bluetooth ativo (menuconfig)

## Hardware

| Pino   | Uso              |
|--------|------------------|
| GPIO 32 | Botão de alerta |
| GPIO 33 | Jumper de config (LOW = modo configuração) |

### 🎯 Configuração Eletrônica (Placa Atual - Flash 4MB)

**Especificações**:
- Microcontrolador: ESP32
- Flash: 4MB
- Pull-ups: **HABILITADOS** (interno do ESP32)

**Mapa de GPIOs Detalhado**:
| Pino | Função | Configuração | Estado Repouso |
|------|--------|--------------|-----------------|
| GPIO_32 | Botão de Alerta | Input + Pull-up | HIGH (não pressionado) |
| GPIO_33 | Jumper Configuração | Input + Pull-up | HIGH (não ativo) |

### 📋 Alterações Recentes (11 Mar 2026)

**Fix: GPIO Pull-up Enable**

❌ **Problema Original**: Wakeups fantasma contínuos (acordava sozinho)
- **Causa**: GPIO_32 sem pull-up → pino flutuava → EXT0 interpretava como "botão pressionado"

✅ **Solução Implementada**:
```cpp
// ANTES (causava problema)
btn_cfg.pull_up_en = GPIO_PULLUP_DISABLE;

// AGORA (corrigido)
btn_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
```

**Arquivos Modificados**:
1. `components/app/app.cpp` - Linha 21: Habilitado pull-up do botão
2. `components/sys/sys.cpp` - Linhas 14, 23: Configuração de EXT0 wakeup e pull-up do jumper

### 🚀 Adaptação para Outra Placa

**Se mudar de placa, modifique**:

1. **GPIO do botão** (`components/app/app.cpp` - Linha 13):
   ```cpp
   static const gpio_num_t BTN = GPIO_NUM_XX;  // Seu pino aqui
   ```

2. **GPIO do jumper** (`components/app/app.cpp` - Linha 14):
   ```cpp
   static const gpio_num_t JUMPER = GPIO_NUM_YY;  // Seu pino aqui
   ```

3. **Se o botão usar pull-down** (ao invés de pull-up):
   ```cpp
   // components/app/app.cpp - Linhas 21-22
   btn_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
   btn_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;

   // components/sys/sys.cpp - Linha 14
   esp_sleep_enable_ext0_wakeup(wake_btn_pin, 1);  // 1 = HIGH trigger
   ```

**Referência rápida**:
| Cenário | Pull-up | Pull-down | EXT0 |
|---------|---------|----------|------|
| Placa atual (4MB) | ENABLE | DISABLE | 0 (LOW) |
| Com resistor externo | ENABLE | DISABLE | 0 (LOW) |
| Com pull-down | DISABLE | ENABLE | 1 (HIGH) |

**⚡ Dicas para outra placa**:
- Sempre verificar os logs `DEBUG BOOT: Nível inicial BTN = ?` durante testes
- Se houver wakeups fantasma: aumentar resistor pull-up (22kΩ) ou adicionar capacitor 0.1µF
- Se o botão não acordar: verificar trigger do EXT0 (0 ou 1) ou tentar EXT1 para múltiplos pinos

## Uso

1. **Configuração**: jumper em GPIO 33 para GND. App BLE "WM Alert Config" envia `machine_id` e `alert_type`. Remover jumper para sair.
2. **Alerta**: acordar com o botão (GPIO 32). Segurar 3 s ou gesto válido para enviar alerta. 5 cliques rápidos abre modo configuração.

## Build

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Estrutura

- `main/` — entrada (`Storage::init`, `App::run`)
- `components/app` — orquestração (jumper vs modo alerta)
- `components/alert_runner` — modo alerta (gesto, envio BLE, sleep)
- `components/button` — detecção de gestos
- `components/sys` — deep sleep e jumper
- `components/storage` — NVS (machine_id, alert_type)
- `components/ble_config` — servidor BLE de configuração
- `components/ble_alert` — cliente BLE para enviar alerta ao node
