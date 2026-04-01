# device-alert

Firmware ESP32 para dispositivo de alerta por botão. Acorda por GPIO, envia evento via BLE para um node da mesh e volta a deep sleep 

## Requisitos

- ESP-IDF 5.x
- Bluetooth ativo (menuconfig)

## Hardware

| Pino   | Uso              |
|--------|------------------|
| GPIO 32 | Botão de alerta |
| GPIO 33 | Jumper de config (LOW = modo configuração) |

### 🔧 Escolha do tipo de placa (obrigatório)

O firmware suporta dois tipos de placa. **Sempre defina qual placa está usando** em:

**Arquivo:** `components/app/board_config.h`

| Placa | Define | Wake | Pull-up botões | Espera botão soltar |
|-------|--------|------|-----------------|---------------------|
| **4MB** (1 botão, flash) | `BOARD_4MB_SINGLE_BUTTON` | EXT0 (GPIO botão) | Interno (ENABLE) | Sem timeout (aguarda HIGH) |
| **8MB OEE** (vários botões) | `BOARD_8MB_MULTI_BUTTON` | EXT1 (máscara) | Externo (DISABLE) | 3000 ms |

No `board_config.h`, deixe **uma** linha ativa e a outra comentada, por exemplo para 4MB:

```c
#define BOARD_4MB_SINGLE_BUTTON  1
/* #define BOARD_8MB_MULTI_BUTTON  1 */
```

Para 8MB OEE:

```c
/* #define BOARD_4MB_SINGLE_BUTTON  1 */
#define BOARD_8MB_MULTI_BUTTON  1
```

---

### 🎯 Placa 4MB (um botão, flash)

- **Flash:** 4MB  
- **Botão:** 1 (ex.: GPIO 32), pull-up **interno** do ESP32.  
- **Wake:** **EXT0** no pino do botão (LOW).  
- **Comportamento:** igual à `main` antiga funcional: no boot espera o botão ficar em HIGH, entra em deep sleep e só acorda quando o botão é pressionado.  
- **Jumper:** GPIO 33 não é usado na 4MB.

### 🎯 Placa 8MB OEE (vários botões)

- **Flash:** 8MB  
- **Botões:** Vários, **sem** pull-up no hardware (resistor externo). No código o pull-up fica **desabilitado**.  
- **Comportamento:** Após enviar o alerta, o firmware espera até **3000 ms** por todos os botões serem soltos.  
- **Compatível com:** comportamento da branch `dev_oee` (pull-up disable, wait completo).

### Mapa de GPIOs (comum)

| Pino    | Função            | Configuração   | Estado repouso      |
|---------|-------------------|----------------|---------------------|
| GPIO_32 | Botão de alerta   | Input + conf. conforme placa | Conforme pull-up   |
| GPIO_33 | Jumper configuração | Input + Pull-up | HIGH (não ativo)  |

### Onde mudar no código (resumo)

| O que mudar | Arquivo | O que fazer |
|-------------|---------|-------------|
| **Tipo de placa (4MB vs 8MB OEE)** | `components/app/board_config.h` | Ativar só `BOARD_4MB_SINGLE_BUTTON` ou `BOARD_8MB_MULTI_BUTTON` |
| **GPIO do(s) botão(ões)** | `components/app/app.cpp` | Array `BUTTONS_CONFIG[]` (pinos e IDs) |
| **GPIO do jumper** | `components/app/app.cpp` | Constante `JUMPER` |
| **Pull-up do jumper** | `components/sys/sys.cpp` | `cfg.pull_up_en` em `is_config_jumper_active` (se sua placa exigir) |

**Referência rápida**:
| Cenário        | Wake | Pull-up botão | Timeout wait release |
|----------------|------|---------------|----------------------|
| Placa 4MB      | EXT0 | ENABLE        | Sem timeout          |
| Placa 8MB OEE  | EXT1 | DISABLE       | 3000 ms              |

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
