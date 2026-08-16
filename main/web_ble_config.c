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
#include "ble_cent.h"
#include "bt_hid_host.h"

extern void hidd_send_volume(bool volume_up);
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"

static const char *TAG = "WEB_BLE_CFG";

/* =====================================================================
 *                    自定义 GATT 服务 UUID
 * ===================================================================== */

// Service: 12345678-1234-1234-1234-123456789abcn
#define WEB_CFG_SVC_UUID128  \
    0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, \
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12

// Characteristic: 87654321-4321-4321-4321-cba987654321 (Read + Write)
#define WEB_CFG_CHAR_UUID128 \
    0x21, 0x43, 0x65, 0x87, 0xa9, 0xcb, 0x21, 0x43, \
    0x21, 0x43, 0x21, 0x43, 0x21, 0x43, 0x65, 0x87

#define WEB_CFG_APP_ID      0x7777   /* GATT 应用 ID */
#define WEB_CFG_CHAR_LEN    64       /* Characteristic 最大长度（单条日志/NVS 数据）*/
#define WEB_CFG_LOG_BUF_LEN 512      /* 累积日志缓冲长度（供 HTML 轮询读取）*/

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

/* 日志内存缓冲（累积多条日志，避免在 BLE 回调中写 NVS 导致崩溃）*/
static char     g_log_buf[WEB_CFG_LOG_BUF_LEN] = {0};
static uint16_t g_log_len = 0;

/* =====================================================================
 *                    GATT 事件回调
 * ===================================================================== */

static void web_cfg_handle_command(const char *cmd);

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

                // 优先返回内存中的最新日志，否则返回 NVS 数据
                if (g_log_len > 0) {
                    rsp.attr_value.len = g_log_len;
                    memcpy(rsp.attr_value.value, g_log_buf, g_log_len);
                } else {
                    rsp.attr_value.len = g_cfg_data_len;
                    memcpy(rsp.attr_value.value, g_cfg_data, g_cfg_data_len);
                }
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                             param->read.trans_id, ESP_GATT_OK, &rsp);
            }
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == char_handle) {
                uint16_t len = param->write.len;
                if (len > WEB_CFG_CHAR_LEN) len = WEB_CFG_CHAR_LEN;

                // 检查是否是命令（以 "CMD:" 开头）
                if (len >= 4 && memcmp(param->write.value, "CMD:", 4) == 0) {
                    char cmd[WEB_CFG_CHAR_LEN];
                    memcpy(cmd, param->write.value, len);
                    cmd[len] = '\0';
                    web_cfg_handle_command(cmd + 4);
                } else {
                    // 保存到 NVS
                    web_cfg_nvs_save(param->write.value, len);
                }

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

/* =====================================================================
 *                    日志推送（通过 NVS 存储，浏览器定时轮询读取）
 * ===================================================================== */

void web_ble_config_log(const char *format, ...)
{
    char buf[WEB_CFG_CHAR_LEN];

    // 1. 同时输出到串口（便于 monitor 调试）
    va_list args;
    va_start(args, format);
    vsnprintf(buf, WEB_CFG_CHAR_LEN, format, args);
    va_end(args);
    ESP_LOGI(TAG, "%s", buf);

    // 2. 追加写入累积日志缓冲供 HTML 轮询
    int len = strlen(buf);
    if (len >= WEB_CFG_CHAR_LEN) len = WEB_CFG_CHAR_LEN - 1;

    // 剩余空间不足则清空重来（保证最新日志优先显示）
    if (g_log_len + len + 2 > WEB_CFG_LOG_BUF_LEN) {
        g_log_len = 0;
        g_log_buf[0] = '\0';
    }
    memcpy(g_log_buf + g_log_len, buf, len);
    g_log_len += len;
    g_log_buf[g_log_len++] = '\n';
    g_log_buf[g_log_len] = '\0';
}

/* =====================================================================
 *                    命令处理
 * ===================================================================== */

static void web_cfg_handle_command(const char *cmd)
{
    ESP_LOGI(TAG, "CMD: %s", cmd);
    if (strcmp(cmd, "SCAN") == 0) {
        ble_cent_start_scan();
    } else if (strncmp(cmd, "CONNECT ", 8) == 0) {
        // CMD:CONNECT aabbccddeeff type
        // 解析地址和地址类型（type 可选，默认 public）
        const char *p = cmd + 8;
        char addr[13] = {0};
        uint8_t addr_type = BLE_ADDR_TYPE_PUBLIC;
        int i = 0;
        while (p[i] != '\0' && p[i] != ' ' && i < 12) {
            addr[i] = p[i];
            i++;
        }
        addr[i] = '\0';
        if (p[i] == ' ') {
            addr_type = (uint8_t)atoi(p + i + 1);
        }
        ble_cent_connect(addr, addr_type);
    } else if (strcmp(cmd, "BTSCAN") == 0) {
        bt_hid_host_start_discovery();
    } else if (strncmp(cmd, "BTCONNECT ", 10) == 0) {
        // CMD:BTCONNECT aabbccddeeff（经典蓝牙 HID 设备地址，无冒号）
        const char *p = cmd + 10;
        char addr[13] = {0};
        int i = 0;
        while (p[i] != '\0' && p[i] != ' ' && i < 12) {
            addr[i] = p[i];
            i++;
        }
        addr[i] = '\0';
        bt_hid_host_connect(addr);
    } else if (strcmp(cmd, "VOLUP") == 0) {
        hidd_send_volume(true);
    } else if (strcmp(cmd, "VOLDOWN") == 0) {
        hidd_send_volume(false);
    } else {
        web_ble_config_log("未知命令: %s", cmd);
    }
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
