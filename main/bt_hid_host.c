/*
 * 经典蓝牙 HID Host 模块 — 连接经典蓝牙键盘/鼠标
 *
 * ESP32 作为经典蓝牙 HID Host（主设备）：
 *   1. 设备发现（Inquiry）
 *   2. 配对（PIN 码）
 *   3. 连接（esp_bt_hid_host_connect，内部自动 SDP + L2CAP）
 *   4. 接收输入报告（ESP_HIDH_DATA_IND_EVT）→ 转发给 BLE 电脑
 *
 * 结果通过 web_ble_config_log 推送（BTDEV|addr|rssi|name 格式）。
 */

#include "bt_hid_host.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_hidh_api.h"
#include "web_ble_config.h"

/* 转发键盘到 BLE 电脑（在 ble_hidd_demo_main.c 中定义）*/
extern void hidd_forward_keyboard(uint8_t special_key_mask, uint8_t *keyboard_cmd, uint8_t num_key);
/* 转发鼠标到 BLE 电脑（在 ble_hidd_demo_main.c 中定义）*/
extern void hidd_forward_mouse(uint8_t mouse_button, int8_t mickeys_x, int8_t mickeys_y, int8_t wheel, int8_t ac_pan);

#define BT_INQ_LEN 10   /* 设备发现时长，单位 1.28s，10 = 约 12.8 秒 */
#define BT_PIN_CODE "0000"  /* 经典蓝牙配对 PIN 码 */

static bool g_bt_hid_inited = false;
static bool g_bt_discovering = false;
static uint8_t g_bt_dev_count = 0;

/* 键盘事件队列（解耦 HID 回调与 BLE 转发）*/
typedef struct {
    uint8_t mod;          /* 修饰键位图 */
    uint8_t keys[6];      /* 按键数组 */
    uint8_t num;          /* 按键数量 */
} bt_key_event_t;

static QueueHandle_t g_bt_key_queue = NULL;
static void bt_key_send_task(void *param);

/* =====================================================================
 *                    HID Host 回调
 * ===================================================================== */

static void bt_hid_host_cb(esp_hidh_cb_event_t event, esp_hidh_cb_param_t *param)
{
    switch (event) {
    case ESP_HIDH_INIT_EVT:
        if (param->init.status == ESP_HIDH_OK) {
            g_bt_hid_inited = true;
            web_ble_config_log("[BTH] 经典蓝牙 HID Host 初始化成功");
        } else {
            web_ble_config_log("[BTH] HID Host 初始化失败: %d", param->init.status);
        }
        break;

    case ESP_HIDH_OPEN_EVT:
        if (param->open.status == ESP_HIDH_OK) {
            web_ble_config_log("[BTH] 已连接 HID 设备, handle=%d", param->open.handle);
        } else {
            web_ble_config_log("[BTH] 连接失败, status=%d", param->open.status);
        }
        break;

    case ESP_HIDH_CLOSE_EVT:
        web_ble_config_log("[BTH] HID 设备断开, reason=0x%02x", param->close.reason);
        break;

    case ESP_HIDH_DATA_IND_EVT: {
        /* 收到 HID 输入报告 */
        uint8_t *data = param->data_ind.data;
        uint16_t len = param->data_ind.len;
        esp_hidh_protocol_mode_t mode = param->data_ind.proto_mode;

        if (mode == ESP_HIDH_BOOT_MODE && len >= 8) {
            /* Boot 键盘报告：byte0=修饰键, byte1=保留, byte2~7=按键数组 */
            bt_key_event_t ev;
            ev.mod = data[0];
            ev.num = 0;
            for (int i = 2; i < 8 && i < len; i++) {
                if (data[i] != 0 && ev.num < 6) {
                    ev.keys[ev.num++] = data[i];
                }
            }
            if (g_bt_key_queue) {
                xQueueSend(g_bt_key_queue, &ev, 0);
            }
        } else if (mode == ESP_HIDH_BOOT_MODE && len >= 3) {
            /* Boot 鼠标报告：byte0=按键, byte1=X, byte2=Y */
            hidd_forward_mouse(data[0], (int8_t)data[1], (int8_t)data[2], 0, 0);
        } else if (mode == ESP_HIDH_REPORT_MODE && len >= 9) {
            /* Report 模式键盘报告（Magic Keyboard 10 字节）：
             * byte0=Report ID, byte1=修饰键, byte2=padding, byte3~8=6 按键数组 */
            bt_key_event_t ev;
            ev.mod = data[1];
            ev.num = 0;
            for (int i = 3; i < 9 && i < len; i++) {
                if (data[i] != 0 && ev.num < 6) {
                    ev.keys[ev.num++] = data[i];
                }
            }
            if (g_bt_key_queue) {
                xQueueSend(g_bt_key_queue, &ev, 0);
            }
        } else {
            /* 其他报告：打印原始数据 hex，便于分析 */
            static uint32_t rpt_count = 0;
            if ((++rpt_count % 5) == 1) {
                char hexbuf[80] = {0};
                int pos = 0;
                for (int i = 0; i < len && pos < 76; i++) {
                    pos += snprintf(hexbuf + pos, sizeof(hexbuf) - pos, "%02x ", data[i]);
                }
                web_ble_config_log("[BTH] Report(%d字节) mode=%d: %s", len, mode, hexbuf);
            }
        }
        break;
    }

    case ESP_HIDH_GET_DSCP_EVT:
        web_ble_config_log("[BTH] 收到报告描述符 len=%d", param->dscp.dl_len);
        ESP_LOG_BUFFER_HEX("[BTH_DSCP]", param->dscp.dsc_list, param->dscp.dl_len);
        break;

    default:
        break;
    }
}

/* =====================================================================
 *                    GAP 回调（设备发现 + 配对）
 * ===================================================================== */

void bt_hid_host_gap_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        /* 设备发现结果：提取地址、名称、RSSI、COD */
        char dev_name[32] = "(未知)";
        int8_t rssi = 0;
        bool has_rssi = false;

        for (int i = 0; i < param->disc_res.num_prop; i++) {
            esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
            if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->len > 0) {
                int n = p->len < 31 ? p->len : 31;
                memcpy(dev_name, p->val, n);
                dev_name[n] = '\0';
            } else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI && p->len >= 1) {
                rssi = *(int8_t *)p->val;
                has_rssi = true;
            }
        }

        g_bt_dev_count++;
        web_ble_config_log("BTDEV|%02x%02x%02x%02x%02x%02x|%d|%s",
                           param->disc_res.bda[0], param->disc_res.bda[1],
                           param->disc_res.bda[2], param->disc_res.bda[3],
                           param->disc_res.bda[4], param->disc_res.bda[5],
                           has_rssi ? rssi : 0, dev_name);
        break;
    }

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            web_ble_config_log("[BTH] 经典蓝牙设备发现已启动...");
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            g_bt_discovering = false;
            web_ble_config_log("[BTH] 设备发现完成, 共发现 %d 个设备", g_bt_dev_count);
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT:
        /* 配对 PIN 请求：回复固定 PIN */
        web_ble_config_log("[BTH] PIN 配对请求, 回复 " BT_PIN_CODE);
        {
            esp_bt_pin_code_t pin_code;
            int plen = strlen(BT_PIN_CODE);
            memset(pin_code, 0, sizeof(pin_code));
            memcpy(pin_code, BT_PIN_CODE, plen);
            esp_bt_gap_pin_reply(param->pin_req.bda, true, plen, pin_code);
        }
        break;

    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            web_ble_config_log("[BTH] 配对成功: %s", param->auth_cmpl.device_name);
        } else {
            web_ble_config_log("[BTH] 配对失败, status=%d", param->auth_cmpl.stat);
        }
        break;

    case ESP_BT_GAP_CFM_REQ_EVT:
        /* SSP 确认请求：自动确认 */
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    default:
        break;
    }
}

/* =====================================================================
 *                    键盘转发任务
 * ===================================================================== */

static void bt_key_send_task(void *param)
{
    bt_key_event_t ev;
    while (1) {
        if (xQueueReceive(g_bt_key_queue, &ev, portMAX_DELAY) == pdTRUE) {
            hidd_forward_keyboard(ev.mod, ev.keys, ev.num);
        }
    }
}

/* =====================================================================
 *                    公开 API
 * ===================================================================== */

void bt_hid_host_init(void)
{
    /* 注册经典蓝牙 GAP 回调（设备发现 + 配对）*/
    esp_bt_gap_register_callback(bt_hid_host_gap_handler);

    /* 键盘事件队列 + 转发任务 */
    g_bt_key_queue = xQueueCreate(32, sizeof(bt_key_event_t));
    if (g_bt_key_queue) {
        xTaskCreate(bt_key_send_task, "bt_key_send", 4096, NULL, 5, NULL);
    }

    /* 设置经典蓝牙配对参数：固定 PIN */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code;
    int plen = strlen(BT_PIN_CODE);
    memset(pin_code, 0, sizeof(pin_code));
    memcpy(pin_code, BT_PIN_CODE, plen);
    esp_bt_gap_set_pin(pin_type, plen, pin_code);

    /* 设置设备可被连接、可被发现（经典蓝牙）*/
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    /* 注册 HID Host 回调并初始化 */
    esp_bt_hid_host_register_callback(bt_hid_host_cb);
    esp_err_t ret = esp_bt_hid_host_init();
    if (ret != ESP_OK) {
        web_ble_config_log("[BTH] HID Host init 调用失败: %d", ret);
    }
}

void bt_hid_host_start_discovery(void)
{
    if (g_bt_discovering) {
        web_ble_config_log("[BTH] 设备发现正在进行中...");
        return;
    }
    g_bt_discovering = true;
    g_bt_dev_count = 0;
    web_ble_config_log("[BTH] 开始经典蓝牙设备发现（约 %d 秒）...", BT_INQ_LEN * 128 / 100);

    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, BT_INQ_LEN, 0);
    if (ret != ESP_OK) {
        g_bt_discovering = false;
        web_ble_config_log("[BTH] 设备发现启动失败: %d", ret);
    }
}

void bt_hid_host_connect(const char *addr_str)
{
    esp_bd_addr_t addr;
    /* 解析 "aabbccddeeff" 格式地址 */
    for (int i = 0; i < 6; i++) {
        char byte_str[3] = {addr_str[i * 2], addr_str[i * 2 + 1], '\0'};
        addr[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }

    web_ble_config_log("[BTH] 正在连接经典蓝牙 HID 设备 %s...", addr_str);
    esp_err_t ret = esp_bt_hid_host_connect(addr);
    if (ret != ESP_OK) {
        web_ble_config_log("[BTH] 连接调用失败: %d", ret);
    }
}
