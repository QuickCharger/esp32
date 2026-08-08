/*
 * Web Bluetooth 配置服务 — 浏览器通过 BLE GATT 读写 ESP32 NVS 数据
 *
 * 架构：
 *   浏览器 ←→ BLE GATT ←→ ESP32 NVS
 *
 * 在 BLE 设备上新增一个自定义 GATT Service：
 *   Service UUID:     12345678-1234-1234-1234-123456789abc
 *   Char UUID (R/W):  87654321-4321-4321-4321-cba987654321
 *
 * 浏览器通过 Web Bluetooth API 连接后：
 *   await characteristic.writeValue(data)  → 保存到 NVS
 *   await characteristic.readValue()       → 从 NVS 读取
 */

#include "web_ble_config.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"

static const char *TAG = "WEB_BLE_CFG";

/* =====================================================================
 *                    自定义 GATT 服务 UUID
 * ===================================================================== */

// Service: 12345678-1234-1234-1234-123456789abc
#define WEB_CFG_SVC_UUID128  \
    0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, \
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12

// Characteristic: 87654321-4321-4321-4321-cba987654321 (Read + Write)
#define WEB_CFG_CHAR_UUID128 \
    0x21, 0x43, 0x65, 0x87, 0xa9, 0xcb, 0x21, 0x43, \
    0x21, 0x43, 0x21, 0x43, 0x21, 0x43, 0x65, 0x87

#define WEB_CFG_APP_ID      0x7777   /* GATT 应用 ID */
#define WEB_CFG_CHAR_LEN    64       /* Characteristic 最大长度 */

/* =====================================================================
 *                    NVS 存储
 * ===================================================================== */

#define WEB_CFG_NVS_NS  "webcfg"
#define WEB_CFG_NVS_KEY "data"

static uint8_t  g_cfg_data[WEB_CFG_CHAR_LEN] = {0};
static uint16_t g_cfg_data_len = 0;

static void web_cfg_nvs_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(WEB_CFG_NVS_NS, NVS_READONLY, &handle) != ESP_OK) return;
    size_t len = WEB_CFG_CHAR_LEN;
    if (nvs_get_blob(handle, WEB_CFG_NVS_KEY, g_cfg_data, &len) == ESP_OK) {
        g_cfg_data_len = len;
        ESP_LOGI(TAG, "Loaded %d bytes from NVS", len);
    }
    nvs_close(handle);
}

static esp_err_t web_cfg_nvs_save(const uint8_t *data, uint16_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WEB_CFG_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, WEB_CFG_NVS_KEY, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        if (err == ESP_OK) {
            memcpy(g_cfg_data, data, len);
            g_cfg_data_len = len;
            ESP_LOGI(TAG, "Saved %d bytes to NVS", len);
        }
    }
    nvs_close(handle);
    return err;
}

/* =====================================================================
 *                    GATT 属性表
 * ===================================================================== */

enum {
    IDX_SVC,
    IDX_CHAR_DECL,
    IDX_CHAR_VAL,
    IDX_NB,
};

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t char_decl_uuid       = ESP_GATT_UUID_CHAR_DECLARE;
static uint8_t        char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE;

static uint16_t svc_handle = 0;
static uint16_t char_handle = 0;
static esp_gatt_if_t g_web_cfg_gatts_if = ESP_GATT_IF_NONE;  /* 保存 GATT 接口句柄 */

/* =====================================================================
 *                    GATT 事件回调
 * ===================================================================== */

static void web_cfg_gatts_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                   esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.app_id != WEB_CFG_APP_ID) break; // 不是本模块的事件
            g_web_cfg_gatts_if = gatts_if;  // 保存 GATT 接口句柄
            ESP_LOGI(TAG, "GATT app registered (app_id=0x%04x, gatts_if=%d), creating service...",
                     param->reg.app_id, gatts_if);
            esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){
                .id.inst_id = 0,
                .id.uuid.len = ESP_UUID_LEN_128,
                .id.uuid.uuid.uuid128 = {WEB_CFG_SVC_UUID128},
                .is_primary = true,
            }, IDX_NB);
            break;

        case ESP_GATTS_CREATE_EVT:
            svc_handle = param->create.service_handle;
            ESP_LOGI(TAG, "Service created, handle=%d", svc_handle);

            // 添加 Characteristic Declaration
            esp_ble_gatts_add_char(svc_handle,
                &(esp_bt_uuid_t){.len = ESP_UUID_LEN_128, .uuid.uuid128 = {WEB_CFG_CHAR_UUID128}},
                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                char_prop_read_write,
                NULL, NULL);
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            char_handle = param->add_char.attr_handle;
            ESP_LOGI(TAG, "Characteristic added, handle=%d", char_handle);
            esp_ble_gatts_start_service(svc_handle);
            break;

        case ESP_GATTS_START_EVT:
            ESP_LOGI(TAG, "Web BLE Config Service started!");
            break;

        case ESP_GATTS_READ_EVT:
            if (param->read.handle == char_handle) {
                esp_gatt_rsp_t rsp;
                memset(&rsp, 0, sizeof(rsp));
                rsp.attr_value.handle = char_handle;
                rsp.attr_value.len = g_cfg_data_len;
                memcpy(rsp.attr_value.value, g_cfg_data, g_cfg_data_len);
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                             param->read.trans_id, ESP_GATT_OK, &rsp);
                ESP_LOGI(TAG, "Read request: sent %d bytes", g_cfg_data_len);
            }
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == char_handle) {
                uint16_t len = param->write.len;
                if (len > WEB_CFG_CHAR_LEN) len = WEB_CFG_CHAR_LEN;

                // 保存到 NVS
                web_cfg_nvs_save(param->write.value, len);

                // 发送确认响应
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                 param->write.trans_id, ESP_GATT_OK, NULL);
                }
                ESP_LOGI(TAG, "Write: saved %d bytes to NVS", len);
            }
            break;

        default:
            break;
    }
}

/* =====================================================================
 *                    公开 API
 * ===================================================================== */

void web_ble_config_init(void)
{
    web_cfg_nvs_load();
    esp_ble_gatts_app_register(WEB_CFG_APP_ID);
}

/* 全局 GATT 回调分发（在 ble_hidd_demo_main.c 的 gap_event_handler 中调用）*/
void web_ble_config_gatts_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                   esp_ble_gatts_cb_param_t *param)
{
    // 只要 gatts_if 匹配已保存的接口句柄，或尚未建立连接（ESP_GATT_IF_NONE），就处理
    if (g_web_cfg_gatts_if == ESP_GATT_IF_NONE || gatts_if == g_web_cfg_gatts_if) {
        web_cfg_gatts_handler(event, gatts_if, param);
    }
}
