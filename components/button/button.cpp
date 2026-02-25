#include "button.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "BTN";

static constexpr int64_t DETECTION_WINDOW_US    = 10'000'000;
static constexpr int64_t CLICK_MAX_US           =   600'000;
static constexpr int64_t HOLD_MIN_US            = 3'000'000;
static constexpr int64_t CONFIG_TIMEOUT_US      = 4'000'000;
static constexpr int     CLICK_TARGET           = 5;

struct BtnEvent
{
    bool    pressed;
    int64_t ts_us;
};

static gpio_num_t    s_pin;
static QueueHandle_t s_queue;

static constexpr int64_t DEBOUNCE_US = 50'000;
static int64_t s_last_isr_us = 0;

static void IRAM_ATTR isr_handler(void *)
{
    int64_t now = esp_timer_get_time();
    if ((now - s_last_isr_us) < DEBOUNCE_US) return;
    s_last_isr_us = now;

    BtnEvent ev;
    ev.pressed = (gpio_get_level(s_pin) == 0);
    ev.ts_us   = now;
    xQueueSendFromISR(s_queue, &ev, nullptr);
}

void Button::init(gpio_num_t pin)
{
    s_pin   = pin;
    s_queue = xQueueCreate(32, sizeof(BtnEvent));

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_ANYEDGE;
    gpio_config(&cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, isr_handler, nullptr);

    if (gpio_get_level(pin) == 0) {
        BtnEvent ev = {true, esp_timer_get_time()};
        xQueueSend(s_queue, &ev, 0);
    }
}

Button::Result Button::detect()
{
    int64_t window_start  = esp_timer_get_time();
    int64_t first_click_us = 0;
    int64_t press_start   = -1;
    int     click_count   = 0;
    bool    button_held   = false;

    BtnEvent ev;

    while (true) {
        int64_t now       = esp_timer_get_time();
        int64_t window_ms = (now - window_start) / 1000;

        if (window_ms >= DETECTION_WINDOW_US / 1000) break;

        if (click_count > 0 && click_count < CLICK_TARGET && !button_held && first_click_us > 0) {
            int64_t since_first = (now - first_click_us) / 1000;
            if (since_first >= CONFIG_TIMEOUT_US / 1000) {
                ESP_LOGI(TAG, "Timeout config: %d cliques em %lld ms — tratando como alerta",
                         click_count, since_first);
                return Result::HOLD_3S;
            }
        }

        if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (ev.pressed && !button_held) {
                button_held = true;
                press_start = ev.ts_us;

            } else if (!ev.pressed && button_held) {
                button_held = false;
                int64_t hold_us = ev.ts_us - press_start;
                press_start = -1;

                if (hold_us < CLICK_MAX_US) {
                    click_count++;
                    if (first_click_us == 0) first_click_us = ev.ts_us;
                    ESP_LOGI(TAG, "Clique %d", click_count);
                    if (click_count >= CLICK_TARGET) {
                        ESP_LOGI(TAG, "5 cliques — modo configuração");
                        return Result::FIVE_CLICKS;
                    }
                }
                else if (hold_us >= CLICK_MAX_US) {
                    ESP_LOGI(TAG, "Pressão longa (%lld ms) — tratando como alerta", hold_us / 1000);
                    return Result::HOLD_3S;
                }
            }
        }

        if (button_held && press_start >= 0) {
            int64_t held_us = esp_timer_get_time() - press_start;
            if (held_us >= HOLD_MIN_US) {
                ESP_LOGI(TAG, "Hold 3s detectado — disparando alerta");
                while (xQueueReceive(s_queue, &ev, 0) == pdTRUE) {}
                return Result::HOLD_3S;
            }
        }
    }

    if (click_count > 0) {
        ESP_LOGI(TAG, "Janela encerrada com %d clique(s) — tratando como alerta", click_count);
        return Result::HOLD_3S;
    }

    ESP_LOGI(TAG, "Janela encerrada sem interação — sem ação");
    return Result::NONE;
}
