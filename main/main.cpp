#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "storage.hpp"
#include "ble_config.hpp"
#include "ble_alert.hpp"

static const char *TAG         = "MAIN";
static const gpio_num_t BTN    = GPIO_NUM_32;  // Botão de alerta
static const gpio_num_t JUMPER = GPIO_NUM_33;  // Jumper de configuração (LOW = config)

// ─── Deep sleep — acorda por botão ──────────────────────────────────────────
static void go_deep_sleep()
{
    ESP_LOGI(TAG, "Entrando em deep sleep...");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_sleep_enable_ext0_wakeup(BTN, 0);
    esp_deep_sleep_start();
}

// ─── Verifica se jumper de configuração está ativo ──────────────────────────
static bool is_config_jumper_active()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << JUMPER);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    vTaskDelay(pdMS_TO_TICKS(50)); // estabilizar leitura
    int level = gpio_get_level(JUMPER);
    ESP_LOGI("MAIN", "Jumper GPIO%d nível: %d (0=ativo)", JUMPER, level);
    return (level == 0);
}

// ─── Ponto de entrada ───────────────────────────────────────────────────────
extern "C" void app_main(void)
{
    Storage::init();

    // ── Jumper ativo → modo configuração (loop até remover jumper) ───────────
    if (is_config_jumper_active()) {
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
        ESP_LOGI(TAG, ">> MODO CONFIGURAÇÃO (jumper ativo) <<");
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");

        while (is_config_jumper_active()) {
            BleConfig::start(
                [](BleConfig::ConfigData data) {
                    Storage::save_config(data.machine_id, data.alert_type);
                    ESP_LOGI("MAIN", "Config salva: machine=%s alert_type=%d",
                             data.machine_id.c_str(), data.alert_type);
                },
                60'000,  // timeout generoso — jumper controla a saída
                []() -> bool { return !is_config_jumper_active(); }  // sai quando jumper removido
            );
        }

        ESP_LOGI(TAG, "Jumper removido — saindo do modo configuração");
        go_deep_sleep();
        return;
    }

    // ── Modo normal — botão = alerta ─────────────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI(TAG, "Boot inicial — aguardando botão para acordar");
        go_deep_sleep();
        return;
    }

    ESP_LOGI(TAG, ">> DISPARO DE ALERTA <<");

    if (!Storage::is_configured()) {
        ESP_LOGW(TAG, "Dispositivo não configurado — ignorando disparo");
        go_deep_sleep();
        return;
    }

    BleAlert::AlertData alert = {
        .machine_id = Storage::get_machine_id(),
        .alert_type = Storage::get_alert_type(),
    };

    bool ok = BleAlert::send(alert);
    ESP_LOGI(TAG, "Alerta %s", ok ? "enviado" : "FALHOU");

    // Aguarda o botão ser solto antes de dormir (evita loop de alertas)
    {
        gpio_config_t btn_cfg = {};
        btn_cfg.pin_bit_mask = (1ULL << BTN);
        btn_cfg.mode         = GPIO_MODE_INPUT;
        btn_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
        btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        btn_cfg.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&btn_cfg);

        while (gpio_get_level(BTN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        // Debounce — espera estabilizar
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    go_deep_sleep();
}
