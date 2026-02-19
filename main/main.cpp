#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "button.hpp"
#include "storage.hpp"
#include "ble_config.hpp"
#include "ble_alert.hpp"

static const char *TAG     = "MAIN";
static const gpio_num_t BTN = GPIO_NUM_32;

// ─── Coloca o dispositivo em deep sleep ────────────────────────────────────────
// Acorda apenas com borda de descida no botão (pressão).
static void go_deep_sleep()
{
    ESP_LOGI(TAG, "Entrando em deep sleep...");
    vTaskDelay(pdMS_TO_TICKS(100)); // flush de log
    esp_sleep_enable_ext0_wakeup(BTN, 0);
    esp_deep_sleep_start();
}

// ─── Ponto de entrada ─────────────────────────────────────────────────────────
extern "C" void app_main(void)
{
    Storage::init();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        // Boot inicial (ou reset manual) — dorme imediatamente.
        // O usuário deve pressionar o botão para iniciar qualquer ação.
        ESP_LOGI(TAG, "Boot inicial — aguardando botão para acordar");
        go_deep_sleep();
        return; // nunca alcançado
    }

    // ── Acordou por pressão do botão ──────────────────────────────────────────
    ESP_LOGI(TAG, "Acordou por GPIO — iniciando detecção de padrão");

    Button::init(BTN);
    Button::Result result = Button::detect();

    switch (result) {
    // ── 5 cliques rápidos → modo configuração ─────────────────────────────────
    case Button::Result::FIVE_CLICKS:
        ESP_LOGI(TAG, ">> MODO CONFIGURAÇÃO <<");
        BleConfig::start([](BleConfig::ConfigData data) {
            Storage::save_config(data.machine_id, data.alert_type);
        }, 30'000);
        break;

    // ── Segurou 3 segundos → disparar alerta ──────────────────────────────────
    case Button::Result::HOLD_3S:
        ESP_LOGI(TAG, ">> DISPARO DE ALERTA <<");
        if (!Storage::is_configured()) {
            ESP_LOGW(TAG, "Dispositivo não configurado — ignorando disparo");
            break;
        }
        {
            BleAlert::AlertData alert = {
                .machine_id = Storage::get_machine_id(),
                .alert_type = Storage::get_alert_type(),
            };
            bool ok = BleAlert::send(alert);
            ESP_LOGI(TAG, "Alerta %s", ok ? "enviado" : "FALHOU");
        }
        break;

    // ── Sem padrão reconhecido → volta a dormir ───────────────────────────────
    case Button::Result::NONE:
        ESP_LOGI(TAG, "Sem padrão — voltando a dormir");
        break;
    }

    go_deep_sleep();
}
