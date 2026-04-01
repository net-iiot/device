#include "app.hpp"
#include "board_config.h"
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
static const gpio_num_t JUMPER = GPIO_NUM_33;

// ─── Definição genérica de botões ────────────────────────────────────────
struct Button {
    gpio_num_t pin;
    int id;              // identificador único do botão
    uint8_t alert_type;  // tipo de alerta específico deste botão
};

static const Button BUTTONS_CONFIG[] = {
    {GPIO_NUM_32, 1, 0},  // Botão 1 (type será carregado da NVS)
    //{GPIO_NUM_26, 2, 0},  // Botão 2 (type será carregado da NVS)
    // {GPIO_NUM_36, 3, 0},  // Botão 3 (type será carregado da NVS)
};

static const size_t NUM_BUTTONS = sizeof(BUTTONS_CONFIG) / sizeof(BUTTONS_CONFIG[0]);
static Button BUTTONS[sizeof(BUTTONS_CONFIG) / sizeof(BUTTONS_CONFIG[0])];  // Array mutável para carregar types

// Carrega types dos botões da NVS
static void load_buttons_from_nvs()
{
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        BUTTONS[i] = BUTTONS_CONFIG[i];
        BUTTONS[i].alert_type = Storage::get_button_alert_type(BUTTONS[i].id);
        ESP_LOGI(TAG, "Botão %d carregado: pin=%d, type=%d",
                 BUTTONS[i].id, BUTTONS[i].pin, BUTTONS[i].alert_type);
    }
}

// Constrói máscara de bits para todos os botões
static uint64_t get_buttons_mask()
{
    uint64_t mask = 0;
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        mask |= (1ULL << BUTTONS[i].pin);
    }
    return mask;
}

// Constrói máscara de bits para botões + jumper
static uint64_t get_wake_mask()
{
    return get_buttons_mask() | (1ULL << JUMPER);
}

// Configura todos os botões como entrada
static void config_buttons_input()
{
    uint64_t pin_mask = get_buttons_mask();
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = pin_mask;
    btn_cfg.mode         = GPIO_MODE_INPUT;
#if defined(BOARD_4MB_SINGLE_BUTTON)
    btn_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;   /* Placa 4MB: pull-up interno */
#else
    btn_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;  /* Placa 8MB OEE: resistor externo */
#endif
    btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&btn_cfg);
}

// Detecta qual botão foi pressionado (retorna índice no array)
static int detect_pressed_button_index()
{
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        if (gpio_get_level(BUTTONS[i].pin) == 0) {
            return i;
        }
    }
    return -1;  // Nenhum botão pressionado
}

// Obtém o botão pelo índice
static const Button* get_button(int index)
{
    if (index < 0 || index >= (int)NUM_BUTTONS) return nullptr;
    return &BUTTONS[index];
}

// Aguarda todos os botões serem soltos (timeout: 4MB=500ms, 8MB OEE=3000ms)
static void wait_all_buttons_release()
{
#if defined(BOARD_4MB_SINGLE_BUTTON)
    uint32_t timeout_ms = 500;   /* Placa 4MB: timeout curto para não travar (pino pode flutuar) */
#else
    uint32_t timeout_ms = 3000;  /* Placa 8MB OEE: aguarda todos os botões soltos */
#endif
    uint32_t elapsed = 0;

    ESP_LOGI(TAG, "Aguardando botões serem soltos...");

    while (elapsed < timeout_ms) {
        bool any_pressed = false;
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            int level = gpio_get_level(BUTTONS[i].pin);
            if (level == 0) {
                any_pressed = true;
                ESP_LOGI(TAG, "  Botão %d (GPIO %d) ainda pressionado (nível=%d)", i, BUTTONS[i].pin, level);
            }
        }
        if (!any_pressed) {
            ESP_LOGI(TAG, "Todos os botões soltos!");
            vTaskDelay(pdMS_TO_TICKS(100));
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        elapsed += 50;
    }

    // DEBUG: mostrar nível final dos GPIOs após timeout
    ESP_LOGW(TAG, "Timeout aguardando botões serem soltos após %d ms", timeout_ms);
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        int level = gpio_get_level(BUTTONS[i].pin);
        ESP_LOGI(TAG, "  [DEBUG] Botão %d (GPIO %d): nível=%d (0=LOW/pressionado, 1=HIGH/solto)",
                 i, BUTTONS[i].pin, level);
    }
}

void App::run()
{
#if defined(BOARD_4MB_SINGLE_BUTTON)
    // Fluxo 4MB (baseado na main antiga funcional): EXT0 + único botão.
    const gpio_num_t btn_pin = BUTTONS_CONFIG[0].pin;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        config_buttons_input();
        while (gpio_get_level(btn_pin) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        Sys::go_deep_sleep(btn_pin);
        return;
    }

    // EXT0 já confirmou wake por botão. Não exigir nível LOW aqui, pois clique curto
    // pode voltar a HIGH antes do app iniciar.
    config_buttons_input();
    vTaskDelay(pdMS_TO_TICKS(30));

    if (!Storage::is_configured()) {
        Sys::go_deep_sleep(btn_pin);
        return;
    }

    uint8_t alert_type = Storage::get_button_alert_type(1);
    if (alert_type == 0) {
        // Compatibilidade com config antiga (chave legacy `alert_type`)
        alert_type = Storage::get_alert_type();
    }

    BleAlert::AlertData alert = {
        .machine_id = Storage::get_machine_id(),
        .alert_type = alert_type,
    };
    (void)BleAlert::send(alert);

    while (gpio_get_level(btn_pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    Sys::go_deep_sleep(btn_pin);
    return;
#else
    // Fluxo 8MB OEE (EXT1 + múltiplos botões + jumper de configuração).
    ESP_LOGI(TAG, "Board: 8MB OEE multi-button (sem pull-up, timeout 3s)");
    load_buttons_from_nvs();

    if (Sys::is_config_jumper_active(JUMPER)) {
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
        ESP_LOGI(TAG, ">> MODO CONFIGURAÇÃO (jumper ativo) <<");
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
        while (Sys::is_config_jumper_active(JUMPER)) {
            BleConfig::start(
                [](BleConfig::ConfigData data) {
                    Storage::save_config_with_buttons(data.machine_id, data.alert_types.data(), data.alert_types.size());
                    ESP_LOGI("APP", "Config salva: machine=%s com %d botões",
                             data.machine_id.c_str(), data.alert_types.size());
                },
                60'000,
                []() -> bool { return !Sys::is_config_jumper_active(JUMPER); }
            );
        }
        Sys::go_deep_sleep(get_wake_mask());
        return;
    }

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_EXT1) {
        config_buttons_input();
        vTaskDelay(pdMS_TO_TICKS(100));
        Sys::go_deep_sleep(get_wake_mask());
        return;
    }

    config_buttons_input();
    vTaskDelay(pdMS_TO_TICKS(50));
    int btn_idx = detect_pressed_button_index();
    if (btn_idx == -1) {
        Sys::go_deep_sleep(get_wake_mask());
        return;
    }

    const Button* btn = get_button(btn_idx);
    if (!Storage::is_configured()) {
        Sys::go_deep_sleep(get_wake_mask());
        return;
    }

    BleAlert::AlertData alert = {
        .machine_id = Storage::get_machine_id(),
        .alert_type = btn->alert_type,
    };
    (void)BleAlert::send(alert);

    wait_all_buttons_release();
    Sys::go_deep_sleep(get_wake_mask());
    return;
#endif
}
