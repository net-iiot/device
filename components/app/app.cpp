#include "app.hpp"
#include "alert_runner.hpp"
#include "sys.hpp"
#include "storage.hpp"
#include "ble_config.hpp"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG   = "APP";
static const gpio_num_t BTN    = GPIO_NUM_32;
static const gpio_num_t JUMPER = GPIO_NUM_33;

void App::run()
{
    if (Sys::is_config_jumper_active(JUMPER)) {
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
        ESP_LOGI(TAG, ">> MODO CONFIGURAÇÃO (jumper ativo) <<");
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");

        while (Sys::is_config_jumper_active(JUMPER)) {
            BleConfig::start(
                [](BleConfig::ConfigData data) {
                    Storage::save_config(data.machine_id, data.alert_type);
                    ESP_LOGI(TAG, "Config salva: machine=%s alert_type=%d",
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

    AlertRunner::Result result = AlertRunner::run(BTN);

    if (result == AlertRunner::Result::EnterConfig) {
        BleConfig::start(
            [](BleConfig::ConfigData data) {
                Storage::save_config(data.machine_id, data.alert_type);
                ESP_LOGI(TAG, "Config salva: machine=%s alert_type=%d",
                         data.machine_id.c_str(), data.alert_type);
            },
            60'000,
            []() -> bool { return true; }
        );
        Sys::go_deep_sleep(BTN);
    }
}
