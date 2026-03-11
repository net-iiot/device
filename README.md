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

## Uso

1. **Configuração**: jumper em GPIO 33 para GND. App BLE "WM Alert Config" envia `machine_id` e `alert_type`. Remover jumper para sair.
2. **Alerta**: acordar com o botão (GPIO 32). Segurar 3 s ou gesto válido para enviar alerta. 5 cliques rápidos abre modo configuração.

GPIO_PULLUP_ENABLE na devboard e disable na oee

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
