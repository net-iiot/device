#include "storage.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "STORAGE";
static const char *NVS_NS    = "wetzel_alert";
static const char *KEY_MACH  = "machine_id";
static const char *KEY_TYPE  = "alert_type";  // Legacy
static const char *KEY_CFG   = "configured";

// Gera chave dinâmica para alert_type de um botão específico
static std::string get_button_type_key(int button_id)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "btn_%d_type", button_id);
    return std::string(buf);
}

bool Storage::init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS inválido, apagando...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

bool Storage::is_configured()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;

    uint8_t configured = 0;
    nvs_get_u8(h, KEY_CFG, &configured);
    nvs_close(h);
    return configured == 1;
}

bool Storage::save_config(const std::string &machine_id, uint8_t alert_type)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS: %s", esp_err_to_name(err));
        return false;
    }

    nvs_set_str(h, KEY_MACH, machine_id.c_str());
    nvs_set_u8(h, KEY_TYPE, alert_type);
    nvs_set_u8(h, KEY_CFG, 1);
    err = nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Config salva: machine='%s' type=%u", machine_id.c_str(), alert_type);
    return err == ESP_OK;
}

std::string Storage::get_machine_id()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return "";

    char buf[32] = {};
    size_t len = sizeof(buf);
    nvs_get_str(h, KEY_MACH, buf, &len);
    nvs_close(h);
    return std::string(buf);
}

uint8_t Storage::get_alert_type()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;

    uint8_t t = 0;
    nvs_get_u8(h, KEY_TYPE, &t);
    nvs_close(h);
    return t;
}

bool Storage::save_config_with_buttons(const std::string &machine_id, const uint8_t *alert_types, size_t count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS: %s", esp_err_to_name(err));
        return false;
    }

    nvs_set_str(h, KEY_MACH, machine_id.c_str());

    // Grava type de cada botão
    for (size_t i = 0; i < count; i++) {
        std::string key = get_button_type_key(i + 1);  // button_id começa em 1
        nvs_set_u8(h, key.c_str(), alert_types[i]);
        ESP_LOGI(TAG, "Salvando button %d: type=%u", i + 1, alert_types[i]);
    }

    nvs_set_u8(h, KEY_CFG, 1);
    err = nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Config com %d botões salva", count);
    return err == ESP_OK;
}

bool Storage::save_button_alert_type(int button_id, uint8_t alert_type)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS: %s", esp_err_to_name(err));
        return false;
    }

    std::string key = get_button_type_key(button_id);
    nvs_set_u8(h, key.c_str(), alert_type);
    err = nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Button %d type atualizado: %u", button_id, alert_type);
    return err == ESP_OK;
}

uint8_t Storage::get_button_alert_type(int button_id)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;

    std::string key = get_button_type_key(button_id);
    uint8_t t = 0;
    nvs_get_u8(h, key.c_str(), &t);
    nvs_close(h);
    return t;
}
