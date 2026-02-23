#include "ble_config.hpp"
#include "storage.hpp"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstring>

static const char *TAG = "BLE_CFG";

// UUIDs em little-endian (ESP-IDF exige LSB primeiro)
static const uint8_t CFG_SVC_UUID[16]      = {0x60,0x50,0x40,0x30,0x20,0x10,0x99,0x88,0xDD,0xCC,0xBB,0xAA,0x10,0x00,0x4D,0x57};
static const uint8_t MACHINE_CHAR_UUID[16] = {0x60,0x50,0x40,0x30,0x20,0x10,0x99,0x88,0xDD,0xCC,0xBB,0xAA,0x11,0x00,0x4D,0x57};
static const uint8_t TYPE_CHAR_UUID[16]    = {0x60,0x50,0x40,0x30,0x20,0x10,0x99,0x88,0xDD,0xCC,0xBB,0xAA,0x12,0x00,0x4D,0x57};

enum AttrIdx {
    IDX_SVC,
    IDX_MACHINE_DECL,
    IDX_MACHINE_VAL,
    IDX_TYPE_DECL,
    IDX_TYPE_VAL,
    IDX_COUNT
};

static const uint16_t PRIMARY_SVC_UUID  = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t CHAR_DECLARE_UUID = ESP_GATT_UUID_CHAR_DECLARE;
static const uint8_t  PROP_RW          = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE;

// Valores atuais (carregados do NVS)
static char    s_machine_val[32] = {};
static uint16_t s_machine_val_len = 0;
static uint8_t  s_alert_type = 0;

static const esp_gatts_attr_db_t s_attr_table[IDX_COUNT] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&PRIMARY_SVC_UUID, ESP_GATT_PERM_READ,
         sizeof(CFG_SVC_UUID), sizeof(CFG_SVC_UUID), (uint8_t *)CFG_SVC_UUID}},

    [IDX_MACHINE_DECL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&CHAR_DECLARE_UUID, ESP_GATT_PERM_READ,
         sizeof(PROP_RW), sizeof(PROP_RW), (uint8_t *)&PROP_RW}},

    // RSP_BY_APP: controle manual de read/write
    [IDX_MACHINE_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)MACHINE_CHAR_UUID,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         32, 0, nullptr}},

    [IDX_TYPE_DECL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&CHAR_DECLARE_UUID, ESP_GATT_PERM_READ,
         sizeof(PROP_RW), sizeof(PROP_RW), (uint8_t *)&PROP_RW}},

    [IDX_TYPE_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)TYPE_CHAR_UUID,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         1, 0, nullptr}},
};

static EventGroupHandle_t s_events;
static const int EVT_MACHINE_RCVD = BIT0;
static const int EVT_TYPE_RCVD    = BIT1;
static const int EVT_ALL_RCVD     = EVT_MACHINE_RCVD | EVT_TYPE_RCVD;

static uint16_t s_handle_table[IDX_COUNT] = {};
static uint16_t s_gatts_if  = ESP_GATT_IF_NONE;
static uint16_t s_conn_id   = 0xFFFF;

static BleConfig::ConfigCallback s_callback;

// Advertising
static esp_ble_adv_params_t s_adv_params;

static void init_adv_params()
{
    memset(&s_adv_params, 0, sizeof(s_adv_params));
    s_adv_params.adv_int_min       = 0x20;
    s_adv_params.adv_int_max       = 0x40;
    s_adv_params.adv_type          = ADV_TYPE_IND;
    s_adv_params.own_addr_type     = BLE_ADDR_TYPE_PUBLIC;
    s_adv_params.peer_addr_type    = BLE_ADDR_TYPE_PUBLIC;
    s_adv_params.channel_map       = ADV_CHNL_ALL;
    s_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
}

static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0,
    .manufacturer_len    = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len    = 0,
    .p_service_data      = nullptr,
    .service_uuid_len    = sizeof(CFG_SVC_UUID),
    .p_service_uuid      = (uint8_t *)CFG_SVC_UUID,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
        esp_ble_gap_start_advertising(&s_adv_params);
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                 esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        s_gatts_if = gatts_if;
        esp_ble_gap_set_device_name("WM Alert Config");
        esp_ble_gap_config_adv_data(&s_adv_data);
        esp_ble_gatts_create_attr_tab(s_attr_table, gatts_if, IDX_COUNT, 0);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK &&
            param->add_attr_tab.num_handle == IDX_COUNT) {
            memcpy(s_handle_table, param->add_attr_tab.handles, sizeof(s_handle_table));
            esp_ble_gatts_start_service(s_handle_table[IDX_SVC]);
        } else {
            ESP_LOGE(TAG, "Falha ao criar tabela: status=%d handles=%d",
                     param->add_attr_tab.status, param->add_attr_tab.num_handle);
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        s_conn_id = param->connect.conn_id;
        ESP_LOGI(TAG, "Cliente conectado");
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_conn_id = 0xFFFF;
        ESP_LOGI(TAG, "Desconectado — reiniciando advertising");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    // READ: app lendo valores atuais
    case ESP_GATTS_READ_EVT: {
        esp_gatt_rsp_t rsp = {};
        rsp.attr_value.handle = param->read.handle;

        if (param->read.handle == s_handle_table[IDX_MACHINE_VAL]) {
            rsp.attr_value.len = s_machine_val_len;
            memcpy(rsp.attr_value.value, s_machine_val, s_machine_val_len);
            ESP_LOGI(TAG, "READ machine_id: '%.*s'", s_machine_val_len, s_machine_val);
        } else if (param->read.handle == s_handle_table[IDX_TYPE_VAL]) {
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = s_alert_type;
            ESP_LOGI(TAG, "READ alert_type: %u", s_alert_type);
        }

        esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                     param->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }

    // WRITE: app enviando novos valores
    case ESP_GATTS_WRITE_EVT: {
        uint16_t handle = param->write.handle;
        uint16_t len    = param->write.len;
        uint8_t *val    = param->write.value;

        // Envia resposta se necessário (write com resposta)
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                         param->write.trans_id, ESP_GATT_OK, nullptr);
        }

        if (handle == s_handle_table[IDX_MACHINE_VAL] && len > 0) {
            if (len > sizeof(s_machine_val)) len = sizeof(s_machine_val);
            memset(s_machine_val, 0, sizeof(s_machine_val));
            memcpy(s_machine_val, val, len);
            s_machine_val_len = len;
            ESP_LOGI(TAG, "WRITE machine_id: '%.*s'", len, (char *)val);
            xEventGroupSetBits(s_events, EVT_MACHINE_RCVD);
        } else if (handle == s_handle_table[IDX_TYPE_VAL] && len >= 1) {
            s_alert_type = val[0];
            ESP_LOGI(TAG, "WRITE alert_type: %u", s_alert_type);
            xEventGroupSetBits(s_events, EVT_TYPE_RCVD);
        }

        // Quando ambos recebidos, salva no NVS e fecha conexão
        EventBits_t bits = xEventGroupGetBits(s_events);
        if ((bits & EVT_ALL_RCVD) == EVT_ALL_RCVD && s_conn_id != 0xFFFF) {
            ESP_LOGI(TAG, "Config completa — machine='%s' type=%u",
                     s_machine_val, s_alert_type);
            esp_ble_gatts_close(gatts_if, s_conn_id);
        }
        break;
    }

    default:
        break;
    }
}

void BleConfig::start(ConfigCallback cb, uint32_t timeout_ms)
{
    s_callback = cb;
    s_events   = xEventGroupCreate();
    init_adv_params();

    // Carrega valores atuais do NVS
    std::string machine = Storage::get_machine_id();
    s_alert_type = Storage::get_alert_type();
    memset(s_machine_val, 0, sizeof(s_machine_val));
    if (!machine.empty()) {
        size_t len = machine.size();
        if (len > sizeof(s_machine_val)) len = sizeof(s_machine_val);
        memcpy(s_machine_val, machine.c_str(), len);
        s_machine_val_len = len;
    }
    ESP_LOGI(TAG, "NVS atual: machine='%s' type=%u", machine.c_str(), s_alert_type);

    // Inicializa BLE
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    // Sem PIN — limpa bonds antigos
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_NO_BOND;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_io_cap_t io_cap = ESP_IO_CAP_NONE;
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io_cap, sizeof(io_cap));

    // Remove todos os bonds anteriores
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num > 0) {
        esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
        if (dev_list) {
            esp_ble_get_bond_device_list(&dev_num, dev_list);
            for (int i = 0; i < dev_num; i++) {
                esp_ble_remove_bond_device(dev_list[i].bd_addr);
            }
            free(dev_list);
        }
    }

    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(0);
    esp_ble_gatt_set_local_mtu(517);

    ESP_LOGI(TAG, "Aguardando config BLE (timeout=%lus)...", (unsigned long)(timeout_ms / 1000));

    EventBits_t bits = xEventGroupWaitBits(
        s_events, EVT_ALL_RCVD, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));

    if ((bits & EVT_ALL_RCVD) == EVT_ALL_RCVD) {
        ESP_LOGI(TAG, "Configuração salva com sucesso");
        if (s_callback) {
            std::string m(s_machine_val, s_machine_val_len);
            s_callback({m, s_alert_type});
        }
    } else {
        ESP_LOGW(TAG, "Timeout — nenhuma configuração recebida");
    }

    esp_ble_gap_stop_advertising();
    esp_ble_gatts_app_unregister(0);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    vEventGroupDelete(s_events);
    s_events = nullptr;
}
