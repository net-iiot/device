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
    btn_cfg.pull_up_en   = GPIO_PULLUP_ENABLE; //Disable pra usar resistor externo
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

// Aguarda todos os botões serem soltos (com timeout de 3 segundos)
static void wait_all_buttons_release()
{
    uint32_t timeout_ms = 3000;
    uint32_t elapsed = 0;

    ESP_LOGI(TAG, "Aguardando botões serem soltos...");

    while (elapsed < timeout_ms) {
        bool any_pressed = false;
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            int level = gpio_get_level(BUTTONS[i].pin);
            if (level == 0) {
                any_pressed = true;
                ESP_LOGI(TAG, "  Botão %d (GPIO %d) ainda pressionado", i, BUTTONS[i].pin);
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

    ESP_LOGW(TAG, "Timeout aguardando botões serem soltos após %d ms", timeout_ms);
}

void App::run()
{
    // Carrega tipos de alerta dos botões da NVS
    load_buttons_from_nvs();

    // ── Jumper ativo → modo configuração (loop até remover jumper) ───────────
    if (Sys::is_config_jumper_active(JUMPER)) {
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
        ESP_LOGI(TAG, ">> MODO CONFIGURAÇÃO (jumper ativo) <<");
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");

        while (Sys::is_config_jumper_active(JUMPER)) {
            BleConfig::start(
                [](BleConfig::ConfigData data) {
                    // Salva machine_id e múltiplos alert_types
                    Storage::save_config_with_buttons(data.machine_id, data.alert_types.data(), data.alert_types.size());
                    ESP_LOGI("APP", "Config salva: machine=%s com %d botões",
                             data.machine_id.c_str(), data.alert_types.size());
                },
                60'000,
                []() -> bool { return !Sys::is_config_jumper_active(JUMPER); }
            );
        }

        ESP_LOGI(TAG, "Jumper removido — saindo do modo configuração");
        Sys::go_deep_sleep(get_wake_mask());
        return;
    }

    // ── Modo normal — botão = alerta ─────────────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "Boot inicial — aguardando botão para acordar");
        ESP_LOGI(TAG, "Número de botões: %d", NUM_BUTTONS);
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            ESP_LOGI(TAG, "  Botão %d: GPIO=%d, ID=%d", i, BUTTONS[i].pin, BUTTONS[i].id);
        }
        // Configura os GPIOs
        config_buttons_input();
        ESP_LOGI(TAG, "GPIOs configurados como entrada");
        vTaskDelay(pdMS_TO_TICKS(100));  // Pequeno delay para estabilizar leitura
        ESP_LOGI(TAG, "Entrando em deep sleep");
        Sys::go_deep_sleep(get_wake_mask());
        return;
    }

    // Detecta qual botão foi pressionado
    int btn_idx = detect_pressed_button_index();
    if (btn_idx == -1) {
        ESP_LOGI(TAG, "Wakeup espúrio — nenhum botão pressionado");
        Sys::go_deep_sleep(get_wake_mask());
        return;
    }

    const Button* btn = get_button(btn_idx);
    ESP_LOGI(TAG, ">> DISPARO DE ALERTA - Botão %d (type %d) <<", btn->id, btn->alert_type);

    if (!Storage::is_configured()) {
        ESP_LOGW(TAG, "Dispositivo não configurado — ignorando disparo");
        Sys::go_deep_sleep(get_wake_mask());
        return;
    }

    BleAlert::AlertData alert = {
        .machine_id = Storage::get_machine_id(),
        .alert_type = btn->alert_type,  // Tipo específico do botão
    };

    bool ok = BleAlert::send(alert);
    ESP_LOGI(TAG, "Alerta %s", ok ? "enviado" : "FALHOU");

    // Aguarda todos os botões serem soltos (evita múltiplos disparos)
    wait_all_buttons_release();

    Sys::go_deep_sleep(get_wake_mask());
}
