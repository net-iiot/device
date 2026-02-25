#include "storage.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "STORAGE";
static const char *NVS_NS    = "wetzel_alert";
static const char *KEY_MACH  = "machine_id";
static const char *KEY_TYPE  = "alert_type";
static const char *KEY_CFG   = "configured";

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
