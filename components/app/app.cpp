#include "app.hpp"
#include "sys.hpp"
#include "storage.hpp"
#include "ble_config.hpp"
#include "ble_alert.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG   = "APP";
static const gpio_num_t BTN    = GPIO_NUM_32;
static const gpio_num_t JUMPER = GPIO_NUM_33;

static void config_btn_input()
{
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = (1ULL << BTN);
    btn_cfg.mode         = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&btn_cfg);
}

void App::run()
{
    // ── Jumper ativo → modo configuração (loop até remover jumper) ───────────
    if (Sys::is_config_jumper_active(JUMPER)) {
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
        ESP_LOGI(TAG, ">> MODO CONFIGURAÇÃO (jumper ativo) <<");
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");

        while (Sys::is_config_jumper_active(JUMPER)) {
            BleConfig::start(
                [](BleConfig::ConfigData data) {
                    Storage::save_config(data.machine_id, data.alert_type);
                    ESP_LOGI("APP", "Config salva: machine=%s alert_type=%d",
                             data.machine_id.c_str(), data.alert_type);
                },
                60'000,
                []() -> bool { return !Sys::is_config_jumper_active(JUMPER); }
            );
        }

        ESP_LOGI(TAG, "Jumper removido — saindo do modo configuração");
        Sys::go_deep_sleep(BTN);
        return;
    }

    // ── Modo normal — botão = alerta ─────────────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI(TAG, "Boot inicial — aguardando botão para acordar");
        // Espera soltar o botão se estiver pressionado (evita wake imediato)
        config_btn_input();
        while (gpio_get_level(BTN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        Sys::go_deep_sleep(BTN);
        return;
    }

    // Confirma que o botão está realmente pressionado
    config_btn_input();
    vTaskDelay(pdMS_TO_TICKS(50));
    if (gpio_get_level(BTN) != 0) {
        ESP_LOGI(TAG, "Wakeup espúrio — botão não está pressionado, voltando a dormir");
        Sys::go_deep_sleep(BTN);
        return;
    }

    ESP_LOGI(TAG, ">> DISPARO DE ALERTA <<");

    if (!Storage::is_configured()) {
        ESP_LOGW(TAG, "Dispositivo não configurado — ignorando disparo");
        Sys::go_deep_sleep(BTN);
        return;
    }

    BleAlert::AlertData alert = {
        .machine_id = Storage::get_machine_id(),
        .alert_type = Storage::get_alert_type(),
    };

    bool ok = BleAlert::send(alert);
    ESP_LOGI(TAG, "Alerta %s", ok ? "enviado" : "FALHOU");

    // Aguarda o botão ser solto antes de dormir (evita loop de alertas)
    while (gpio_get_level(BTN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    Sys::go_deep_sleep(BTN);
}
