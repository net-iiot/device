#include "sys.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "SYS";

void Sys::go_deep_sleep(gpio_num_t wake_btn_pin)
{
    go_deep_sleep(1ULL << wake_btn_pin);
}

void Sys::go_deep_sleep(uint64_t wake_pin_mask)
{
    ESP_LOGI(TAG, "Entrando em deep sleep com máscara: 0x%llx", wake_pin_mask);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_sleep_enable_ext1_wakeup(wake_pin_mask, (esp_sleep_ext1_wakeup_mode_t)1);  // 1 = ANY_LOW
    esp_deep_sleep_start();
}

bool Sys::is_config_jumper_active(gpio_num_t jumper_pin)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << jumper_pin);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    vTaskDelay(pdMS_TO_TICKS(50));
    int level = gpio_get_level(jumper_pin);
    ESP_LOGI(TAG, "Jumper GPIO%d nível: %d (0=ativo)", jumper_pin, level);
    return (level == 0);
}
