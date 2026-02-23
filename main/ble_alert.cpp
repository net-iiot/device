#include "ble_alert.hpp"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstring>

static const char *TAG = "BLE_ALERT";

// UUIDs em little-endian (ESP-IDF exige LSB primeiro)
// 574D0001-AABB-CCDD-8899-102030405060
static const uint8_t MESH_SVC_UUID[16]    = {0x60,0x50,0x40,0x30,0x20,0x10,0x99,0x88,0xDD,0xCC,0xBB,0xAA,0x01,0x00,0x4D,0x57};
// 574D0004-AABB-CCDD-8899-102030405060
static const uint8_t BUTTON_CHAR_UUID[16] = {0x60,0x50,0x40,0x30,0x20,0x10,0x99,0x88,0xDD,0xCC,0xBB,0xAA,0x04,0x00,0x4D,0x57};

// ─── Pacote enviado ao node (20 bytes, igual ao ButtonPacket do wetzel-mesh) ──
struct __attribute__((packed)) ButtonPacket
{
    uint8_t version;       // sempre 1
    uint8_t event_type;    // 0=press  1=long_press  2=double_press
    uint8_t battery_pct;   // 0–100 (placeholder: 100)
    char    button_id[16]; // machine_id, null-padded
    uint8_t _reserved;     // 0
};

// ─── Event group ─────────────────────────────────────────────────────────────
static EventGroupHandle_t s_events;
static const int EVT_FOUND    = BIT0;
static const int EVT_CONN     = BIT1;
static const int EVT_SVC_DONE = BIT2;
static const int EVT_WRITTEN  = BIT3;
static const int EVT_ERROR    = BIT4;
static const int EVT_DISC     = BIT5;

// ─── Estado interno ───────────────────────────────────────────────────────────
static esp_gatt_if_t   s_gattc_if     = ESP_GATT_IF_NONE;
static uint16_t        s_conn_id      = 0;
static uint16_t        s_char_handle  = 0;
static uint16_t        s_svc_start    = 0;
static uint16_t        s_svc_end      = 0;
static esp_bd_addr_t   s_target_bda   = {};

static ButtonPacket    s_packet       = {};

// ─── GAP callback (scan) ──────────────────────────────────────────────────────
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (event != ESP_GAP_BLE_SCAN_RESULT_EVT) return;
    if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;

    // Verifica se o device anuncia o SERVICE_UUID do wetzel-mesh
    uint8_t uuid_len = 0;
    uint8_t *uuid_data = esp_ble_resolve_adv_data(
        param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_128SRV_CMPL, &uuid_len);

    if (!uuid_data) {
        uuid_data = esp_ble_resolve_adv_data(
            param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_128SRV_PART, &uuid_len);
    }

    if (uuid_data && uuid_len == 16 && memcmp(uuid_data, MESH_SVC_UUID, 16) == 0) {
        // Evita processar múltiplos resultados
        if (xEventGroupGetBits(s_events) & EVT_FOUND) return;

        memcpy(s_target_bda, param->scan_rst.bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "Node encontrado: %02X:%02X:%02X:%02X:%02X:%02X",
                 s_target_bda[0], s_target_bda[1], s_target_bda[2],
                 s_target_bda[3], s_target_bda[4], s_target_bda[5]);
        xEventGroupSetBits(s_events, EVT_FOUND);
    }
}

// ─── GATTC callback ───────────────────────────────────────────────────────────
static void gattc_event_handler(esp_gattc_cb_event_t event,
                                 esp_gatt_if_t gattc_if,
                                 esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        s_gattc_if = gattc_if;
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Falha ao conectar: %d", param->open.status);
            xEventGroupSetBits(s_events, EVT_ERROR);
            break;
        }
        s_conn_id = param->open.conn_id;
        ESP_LOGI(TAG, "Conectado ao node");
        esp_ble_gattc_search_service(gattc_if, s_conn_id, nullptr);
        xEventGroupSetBits(s_events, EVT_CONN);
        break;

    case ESP_GATTC_SEARCH_RES_EVT: {
        esp_gatt_id_t *srvc_id = &param->search_res.srvc_id;
        if (srvc_id->uuid.len == ESP_UUID_LEN_128 &&
            memcmp(srvc_id->uuid.uuid.uuid128, MESH_SVC_UUID, 16) == 0) {
            s_svc_start = param->search_res.start_handle;
            s_svc_end   = param->search_res.end_handle;
            ESP_LOGI(TAG, "Serviço mesh encontrado: handle 0x%04X–0x%04X",
                     s_svc_start, s_svc_end);
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (s_svc_start == 0) {
            ESP_LOGE(TAG, "Serviço mesh não encontrado no device");
            xEventGroupSetBits(s_events, EVT_ERROR);
            break;
        }
        {
            // Localiza a característica BUTTON_CHAR_UUID
            esp_bt_uuid_t char_uuid;
            char_uuid.len = ESP_UUID_LEN_128;
            memcpy(char_uuid.uuid.uuid128, BUTTON_CHAR_UUID, 16);

            esp_gattc_char_elem_t chars[4] = {};
            uint16_t count = 4;
            esp_gatt_status_t st = esp_ble_gattc_get_char_by_uuid(
                gattc_if, s_conn_id, s_svc_start, s_svc_end,
                char_uuid, chars, &count);

            if (st == ESP_GATT_OK && count > 0) {
                s_char_handle = chars[0].char_handle;
                ESP_LOGI(TAG, "Button char handle: 0x%04X", s_char_handle);
                xEventGroupSetBits(s_events, EVT_SVC_DONE);
            } else {
                ESP_LOGE(TAG, "Button char não encontrado: status=%d count=%d", st, count);
                xEventGroupSetBits(s_events, EVT_ERROR);
            }
        }
        break;

    case ESP_GATTC_WRITE_CHAR_EVT:
        if (param->write.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Pacote enviado com sucesso");
            xEventGroupSetBits(s_events, EVT_WRITTEN);
        } else {
            ESP_LOGE(TAG, "Falha ao escrever: %d", param->write.status);
            xEventGroupSetBits(s_events, EVT_ERROR);
        }
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Desconectado do node");
        xEventGroupSetBits(s_events, EVT_DISC);
        break;

    default:
        break;
    }
}

// ─── API pública ──────────────────────────────────────────────────────────────
bool BleAlert::send(const AlertData &data, uint32_t timeout_ms)
{
    s_events = xEventGroupCreate();

    if (data.alert_type == 0) {
        ESP_LOGE(TAG, "alert_type não configurado (0)");
        vEventGroupDelete(s_events);
        s_events = nullptr;
        return false;
    }

    if (data.machine_id.length() > 15) {
        ESP_LOGW(TAG, "machine_id truncado de %d para 15 chars", (int)data.machine_id.length());
    }

    // Monta pacote
    memset(&s_packet, 0, sizeof(s_packet));
    s_packet.version     = 1;
    s_packet.event_type  = data.alert_type;
    s_packet.battery_pct = 100;
    strncpy(s_packet.button_id, data.machine_id.c_str(), sizeof(s_packet.button_id) - 1);

    // Inicializa BLE
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gattc_register_callback(gattc_event_handler);
    esp_ble_gattc_app_register(0);
    esp_ble_gatt_set_local_mtu(517);

    // Inicia scan
    esp_ble_scan_params_t scan_params = {
        .scan_type          = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval      = 0x50,
        .scan_window        = 0x30,
        .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,
    };
    esp_ble_gap_set_scan_params(&scan_params);
    esp_ble_gap_start_scanning(timeout_ms / 1000 + 5);

    ESP_LOGI(TAG, "Procurando node mesh...");

    bool success = false;

    // 1. Aguarda encontrar node
    EventBits_t bits = xEventGroupWaitBits(s_events, EVT_FOUND | EVT_ERROR,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (!(bits & EVT_FOUND)) {
        ESP_LOGW(TAG, "Nenhum node encontrado no scan");
        goto cleanup;
    }

    esp_ble_gap_stop_scanning();

    // 2. Conecta ao node
    esp_ble_gattc_open(s_gattc_if, s_target_bda, BLE_ADDR_TYPE_PUBLIC, true);
    bits = xEventGroupWaitBits(s_events, EVT_CONN | EVT_ERROR,
                               pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & EVT_CONN)) {
        ESP_LOGW(TAG, "Falha na conexão");
        goto cleanup;
    }

    // 3. Aguarda descoberta de serviços
    bits = xEventGroupWaitBits(s_events, EVT_SVC_DONE | EVT_ERROR,
                               pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & EVT_SVC_DONE)) {
        ESP_LOGW(TAG, "Falha na descoberta de serviços");
        goto cleanup;
    }

    // 4. Escreve o pacote
    esp_ble_gattc_write_char(s_gattc_if, s_conn_id, s_char_handle,
                             sizeof(s_packet), (uint8_t *)&s_packet,
                             ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);

    bits = xEventGroupWaitBits(s_events, EVT_WRITTEN | EVT_ERROR,
                               pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
    success = (bits & EVT_WRITTEN) != 0;

    // 5. Desconecta
    esp_ble_gattc_close(s_gattc_if, s_conn_id);
    xEventGroupWaitBits(s_events, EVT_DISC, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));

cleanup:
    esp_ble_gattc_app_unregister(0);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    vEventGroupDelete(s_events);
    s_events = nullptr;

    return success;
}
