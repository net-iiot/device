#include "alert_runner.hpp"
#include "button.hpp"
#include "sys.hpp"
#include "storage.hpp"
#include "ble_alert.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ALERT";

static void wait_button_release(gpio_num_t pin)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    while (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

AlertRunner::Result AlertRunner::run(gpio_num_t btn_pin)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI(TAG, "Boot inicial — aguardando botão para acordar");
        Sys::go_deep_sleep(btn_pin);
        return Result::Sleep;
    }

    if (!Storage::is_configured()) {
        ESP_LOGW(TAG, "Dispositivo não configurado — ignorando");
        Sys::go_deep_sleep(btn_pin);
        return Result::Sleep;
    }

    wait_button_release(btn_pin);

    Button::init(btn_pin);
    Button::Result r = Button::detect();

    if (r == Button::Result::FIVE_CLICKS) {
        ESP_LOGI(TAG, "5 cliques — modo configuração");
        return Result::EnterConfig;
    }

    if (r == Button::Result::HOLD_3S) {
        ESP_LOGI(TAG, ">> DISPARO DE ALERTA <<");
        BleAlert::AlertData alert = {
            .machine_id = Storage::get_machine_id(),
            .alert_type = Storage::get_alert_type(),
        };
        bool ok = BleAlert::send(alert);
        ESP_LOGI(TAG, "Alerta %s", ok ? "enviado" : "FALHOU");
        wait_button_release(btn_pin);
    }

    Sys::go_deep_sleep(btn_pin);
    return Result::Sleep;
}
