#include "button.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "BTN";

// ─── Constantes de temporização ─────────────────────────────────────────────
static constexpr int64_t DETECTION_WINDOW_US  = 6'000'000; // 6s janela total
static constexpr int64_t CLICK_MAX_US         =   800'000; // <800ms = clique
static constexpr int64_t HOLD_MIN_US          = 3'000'000; // ≥3s = segurou
static constexpr int64_t INTER_CLICK_TIMEOUT_US = 1'200'000; // 1.2s sem atividade encerra sequência
static constexpr int      CLICK_TARGET        = 5;

// ─── Evento de botão (ISR → task) ───────────────────────────────────────────
struct BtnEvent
{
    bool    pressed;  // true = borda de descida, false = borda de subida
    int64_t ts_us;    // esp_timer_get_time() no momento do evento
};

static gpio_num_t    s_pin;
static QueueHandle_t s_queue;

// ─── ISR (IRAM) ──────────────────────────────────────────────────────────────
static void IRAM_ATTR isr_handler(void *)
{
    BtnEvent ev;
    ev.pressed = (gpio_get_level(s_pin) == 0);
    ev.ts_us   = esp_timer_get_time();
    xQueueSendFromISR(s_queue, &ev, nullptr);
}

// ─── API pública ─────────────────────────────────────────────────────────────
void Button::init(gpio_num_t pin)
{
    s_pin   = pin;
    s_queue = xQueueCreate(16, sizeof(BtnEvent));

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_ANYEDGE;
    gpio_config(&cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, isr_handler, nullptr);

    // Se o botão já está pressionado quando acordamos (deep sleep wakeup),
    // injeta um evento PRESS sintético para não perder o início do hold.
    if (gpio_get_level(pin) == 0) {
        BtnEvent ev = {true, esp_timer_get_time()};
        xQueueSend(s_queue, &ev, 0);
    }
}

Button::Result Button::detect()
{
    int64_t window_start  = esp_timer_get_time();
    int64_t last_event_us = window_start;
    int64_t press_start   = -1;   // -1 = botão solto
    int     click_count   = 0;
    bool    button_held   = false;

    BtnEvent ev;

    while (true) {
        int64_t now           = esp_timer_get_time();
        int64_t window_ms     = (now - window_start)  / 1000;
        int64_t inactivity_ms = (now - last_event_us) / 1000;

        // Encerra janela por timeout total
        if (window_ms >= DETECTION_WINDOW_US / 1000) break;

        // Encerra por inatividade após pelo menos 1 clique registrado
        if (click_count > 0 && inactivity_ms >= INTER_CLICK_TIMEOUT_US / 1000) break;

        // Aguarda próximo evento ISR (polling a cada 50ms)
        if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            last_event_us = ev.ts_us;

            if (ev.pressed && !button_held) {
                // Borda de descida: início de pressão
                button_held = true;
                press_start = ev.ts_us;

            } else if (!ev.pressed && button_held) {
                // Borda de subida: botão liberado
                button_held = false;
                int64_t hold_us = ev.ts_us - press_start;
                press_start = -1;

                if (hold_us < CLICK_MAX_US) {
                    // Clique curto
                    click_count++;
                    ESP_LOGI(TAG, "Clique %d", click_count);
                    if (click_count >= CLICK_TARGET) {
                        return Result::FIVE_CLICKS;
                    }
                }
                // Pressões intermediárias (800ms–3s) são ignoradas intencionalmente
            }
        }

        // Verifica hold ativo ≥ 3s enquanto botão está pressionado
        if (button_held && press_start >= 0) {
            int64_t held_us = esp_timer_get_time() - press_start;
            if (held_us >= HOLD_MIN_US) {
                ESP_LOGI(TAG, "Hold 3s detectado — aguardando soltar...");

                // Espera o botão ser solto
                while (gpio_get_level(s_pin) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                button_held = false;

                // Aguarda 500ms para garantir que não há clique adicional
                bool extra = false;
                BtnEvent extra_ev;
                if (xQueueReceive(s_queue, &extra_ev, pdMS_TO_TICKS(500)) == pdTRUE) {
                    if (extra_ev.pressed) extra = true;
                }

                if (!extra) {
                    return Result::HOLD_3S;
                }

                // Havia outro clique — não é hold de segurança, conta como clique
                click_count++;
                press_start = -1;
                last_event_us = esp_timer_get_time();
                ESP_LOGI(TAG, "Hold cancelado por clique extra. Cliques: %d", click_count);
            }
        }
    }

    ESP_LOGI(TAG, "Janela encerrada: %d clique(s)", click_count);
    return Result::NONE;
}
