/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/**
 * @file ble_hidd_demo_main.c
 * @brief BLE HID Device Demo — 应用层主程序
 *
 * 本 Demo 实现了完整的 BLE HID 设备功能，包含 4 种报告类型：
 *   1. 鼠标（Mouse）
 *   2. 键盘 + LED 指示灯（Keyboard + LED output）
 *   3. 消费者控制（Consumer Control：音量、播放等）
 *   4. Vendor 自定义（Win10 不支持，默认关闭）
 *
 * 应用场景：ESP32 作为 BLE HID 设备，连接电脑/手机后自动发送
 *          消费者控制命令（音量+/-），演示 HID 报告发送流程。
 *
 * @note 注意事项：
 *   1. Win10 不支持 Vendor Report，因此 SUPPORT_REPORT_VENDOR 设为 false
 *   2. iPhone HID 加密期间不允许更新连接参数
 *   3. iPhone 在 HID 加密未完成时会提前写入 CCCD，代码通过设置
 *      ESP_GATT_PERM_WRITE_ENCRYPTED 权限来解决，
 *      若看到 GATT_INSUF_ENCRYPTION 错误可忽略
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_hidd_prf_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "driver/gpio.h"
#include "hid_dev.h"
#include "web_ble_config.h"
#include "ble_cent.h"

#define HID_DEMO_TAG "HID_DEMO"       /*!< 日志标签 */

/* =====================================================================
 *                      全局变量
 * ===================================================================== */

/**
 * @brief 多连接管理数据结构
 *
 * hid_conn_ids[]:  存储所有活跃连接的 conn_id，最多 HID_MAX_APPS 个
 * hid_conn_count:  当前活跃连接数
 * sec_conn:        是否至少有一个安全连接
 */
static uint16_t hid_conn_ids[HID_MAX_APPS] = {0};  /*!< 多连接 ID 数组 */
static uint8_t  hid_conn_count = 0;                 /*!< 当前活跃连接数 */
static bool     sec_conn = false;                   /*!< 是否至少有一个安全连接（配对完成）*/
static bool     send_volum_up = false;              /*!< Demo 中控制音量+/-发送状态的标志 */
#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);

/* =====================================================================
 *                    BLE 广播参数配置
 * ===================================================================== */

#define HIDD_DEVICE_NAME            "HID"      /*!< 设备名称（广播中显示的名称）*/

/**
 * @brief HID 服务 UUID（128-bit）
 *
 * 128-bit UUID 中，byte [12] [13] 为 16-bit 短 UUID 值。
 * 此处 [12]=0x12, [13]=0x18 即 HID Service（0x1812）
 */
static uint8_t hidd_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

/** @brief 广播数据配置 */
static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,                             /*!< 不单独设置扫描响应数据 */
    .include_name = true,                              /*!< 广播中包含设备名称 */
    .include_txpower = true,                           /*!< 广播中包含发射功率 */
    .min_interval = ESP_BLE_GAP_CONN_ITVL_MS(7.5),     /*!< 从机最小连接间隔：7.5ms */
    .max_interval = ESP_BLE_GAP_CONN_ITVL_MS(20),      /*!< 从机最大连接间隔：20ms */
    .appearance = 0x03c0,                              /*!< 外观：HID Generic (0x03C0) */
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,            /*!< 广播的 HID 服务 UUID */
    .flag = 0x6,                                       /*!< 广播标志：LE General Discoverable + BR/EDR Not Supported */
};

/** @brief 广播参数配置 */
static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = ESP_BLE_GAP_ADV_ITVL_MS(200), /*!< 最小广播间隔：200ms（释放射频给多连接）*/
    .adv_int_max        = ESP_BLE_GAP_ADV_ITVL_MS(300), /*!< 最大广播间隔：300ms */
    .adv_type           = ADV_TYPE_IND,                /*!< 广播类型：可连接、可扫描、不定向 */
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,        /*!< 使用公共地址 */
    .channel_map        = ADV_CHNL_ALL,                /*!< 在所有广播信道（37/38/39）上广播 */
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY, /*!< 允许任何设备扫描和连接 */
};

/** @brief 停止广播（扫描外设时调用，释放射频）*/
void hidd_adv_stop(void)
{
    esp_ble_gap_stop_advertising();
}

/** @brief 恢复广播（扫描结束后调用）*/
void hidd_adv_start(void)
{
    esp_ble_gap_start_advertising(&hidd_adv_params);
}

/* =====================================================================
 *                    HID 事件回调处理
 * ===================================================================== */

/**
 * @brief HID Device Profile 事件回调函数
 *
 * 处理来自 HID Profile 层的事件通知：
 * - 注册完成 → 设置设备名称和广播数据
 * - BLE 连接 → 记录连接 ID
 * - BLE 断开 → 重新开始广播
 * - LED/Vendor 报告写入 → 打印日志
 */
static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch(event) {
        case ESP_HIDD_EVENT_REG_FINISH: {                    /* HID Profile 注册完成 */
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                esp_ble_gap_set_device_name(HIDD_DEVICE_NAME);      // 设置 BLE 设备名
                esp_ble_gap_config_adv_data(&hidd_adv_data);        // 配置广播数据
            }
            break;
        }
        case ESP_BAT_EVENT_REG: {                            /* 电池服务注册完成 */
            break;
        }
        case ESP_HIDD_EVENT_DEINIT_FINISH:                   /* HID 反初始化完成 */
	     break;
        case ESP_HIDD_EVENT_BLE_CONNECT: {                   /* BLE 连接建立 — 多连接版 */
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_CONNECT, conn_id=%d", param->connect.conn_id);
            // 将新连接的 conn_id 加入数组
            if (hid_conn_count < HID_MAX_APPS) {
                hid_conn_ids[hid_conn_count++] = param->connect.conn_id;
                // 如果还有空闲连接槽位，重新开始广播让其他设备也能发现
                if (hid_conn_count < HID_MAX_APPS) {
                    esp_ble_gap_start_advertising(&hidd_adv_params);
                    ESP_LOGI(HID_DEMO_TAG, "Still have free slots, restart advertising");
                }
            } else {
                ESP_LOGW(HID_DEMO_TAG, "Max connections reached, rejecting conn_id=%d", param->connect.conn_id);
            }
            break;
        }
        case ESP_HIDD_EVENT_BLE_DISCONNECT: {                /* BLE 连接断开 — 多连接版 */
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT, conn_id=%d", param->disconnect.conn_id);
            // 从数组中移除断开的 conn_id
            for (int i = 0; i < hid_conn_count; i++) {
                if (hid_conn_ids[i] == param->disconnect.conn_id) {
                    // 将最后一个元素移到当前位置（O(1) 删除）
                    hid_conn_ids[i] = hid_conn_ids[--hid_conn_count];
                    break;
                }
            }
            // 有空闲连接槽位时，重新开始广播
            if (hid_conn_count < HID_MAX_APPS) {
                if (hid_conn_count == 0) {
                    sec_conn = false;
                }
                esp_ble_gap_start_advertising(&hidd_adv_params);
            }
            break;
        }
        case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT: {   /* 主机写入 Vendor 报告 */
            ESP_LOGI(HID_DEMO_TAG, "%s, ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT", __func__);
            ESP_LOG_BUFFER_HEX(HID_DEMO_TAG, param->vendor_write.data, param->vendor_write.length);
            break;
        }
        case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT: {      /* 主机写入 LED 报告（键盘指示灯状态）*/
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT");
            ESP_LOG_BUFFER_HEX(HID_DEMO_TAG, param->led_write.data, param->led_write.length);
            break;
        }
        default:
            break;
    }
    return;
}

/* =====================================================================
 *                    GAP 事件回调处理
 * ===================================================================== */

/**
 * @brief BLE GAP 事件回调函数
 *
 * 处理 BLE 链路层事件：
 * - 广播数据设置完成 → 开始广播
 * - 安全请求 → 响应允许
 * - 认证完成 → 记录配对结果
 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:              /* 广播数据设置完成 */
        esp_ble_gap_start_advertising(&hidd_adv_params);           // 开始广播
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:                            /* 对端发起安全请求 */
        for(int i = 0; i < ESP_BD_ADDR_LEN; i++) {
             ESP_LOGD(HID_DEMO_TAG, "%x:",param->ble_security.ble_req.bd_addr[i]);
        }
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true); // 接受安全请求
	 break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:                          /* 配对/认证完成 */
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_LOGI(HID_DEMO_TAG, "remote BD_ADDR: %08x%04x",
                (bd_addr[0] << 24) + (bd_addr[1] << 16) + (bd_addr[2] << 8) + bd_addr[3],
                (bd_addr[4] << 8) + bd_addr[5]);
        ESP_LOGI(HID_DEMO_TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
        ESP_LOGI(HID_DEMO_TAG, "pair status = %s",param->ble_security.auth_cmpl.success ? "success" : "fail");
        if (param->ble_security.auth_cmpl.success) {
            sec_conn = true;                                       // 标记安全连接已建立
            ESP_LOGI(HID_DEMO_TAG, "secure connection established.");
        } else {
            ESP_LOGE(HID_DEMO_TAG, "pairing failed, reason = 0x%x",
                     param->ble_security.auth_cmpl.fail_reason);
        }
        break;
    default:
        break;
    }
    /* 转发给 BLE Central 模块处理扫描事件 */
    ble_cent_gap_handler(event, param);
}

/* =====================================================================
 *                    Demo 任务
 * ===================================================================== */

/**
 * @brief HID Demo 任务
 *
 * 在安全连接建立后，每 2 秒发送一次消费者控制命令（音量+、音量-）。
 * 这是一个循环演示任务，实际应用中应替换为真正的键盘/鼠标输入处理。
 */
/**
 * @brief 向所有已连接的电脑发送音量控制
 *
 * 由 HTML 按钮触发，通过命令调用。不再自动定时发送。
 *
 * @param volume_up true=音量增大, false=音量降低
 */
void hidd_send_volume(bool volume_up)
{
    if (!sec_conn || hid_conn_count == 0) {
        web_ble_config_log("[HID] 无安全连接，无法发送音量");
        return;
    }

    uint8_t cmd = volume_up ? HID_CONSUMER_VOLUME_UP : HID_CONSUMER_VOLUME_DOWN;

    // 按下
    for (int i = 0; i < hid_conn_count; i++) {
        esp_hidd_send_consumer_value(hid_conn_ids[i], cmd, true);
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);   // 短暂保持
    // 释放
    for (int i = 0; i < hid_conn_count; i++) {
        esp_hidd_send_consumer_value(hid_conn_ids[i], cmd, false);
    }

    web_ble_config_log("[HID] 已向 %d 台设备发送%s", hid_conn_count, volume_up ? "音量+" : "音量-");
}

/**
 * @brief 转发鼠标数据给所有已连接的电脑
 *
 * 由 BLE Central 模块收到鼠标报告后调用。
 *
 * @param mouse_button  按键状态（位掩码，bit0=左键 bit1=右键 bit2=中键 ...）
 * @param mickeys_x     X 轴移动量
 * @param mickeys_y     Y 轴移动量
 * @param wheel         垂直滚轮
 * @param ac_pan        水平滚轮
 */
void hidd_forward_mouse(uint8_t mouse_button, int8_t mickeys_x, int8_t mickeys_y, int8_t wheel, int8_t ac_pan)
{
    if (hid_conn_count == 0) return;

    for (int i = 0; i < hid_conn_count; i++) {
        esp_hidd_send_mouse_value(hid_conn_ids[i], mouse_button, mickeys_x, mickeys_y, wheel, ac_pan);
    }
}

void hid_demo_task(void *pvParameters)
{
    // Demo 任务已停用自动音量发送，仅保留任务占位
    while(1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/* =====================================================================
 *                    主函数 — 系统初始化流程
 * ===================================================================== */

/**
 * @brief 应用主入口
 *
 * 初始化流程：
 *   1. NVS 初始化（存储配对信息等持久数据）
 *   2. 释放经典蓝牙内存（仅使用 BLE）
 *   3. 初始化蓝牙控制器
 *   4. 启用 BLE 模式
 *   5. 初始化并启用 Bluedroid 协议栈
 *   6. 初始化 HID Device Profile
 *   7. 注册 GAP 和 HID 回调
 *   8. 配置安全参数（配对/加密）
 *   9. 创建 Demo 任务
 */
void app_main(void)
{
    esp_err_t ret;

    // 第1步：初始化 NVS（Non-Volatile Storage，用于存储配对信息）
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());                    // NVS 分区损坏则擦除重建
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // 第2步：释放经典蓝牙控制器内存（本项目仅使用 BLE，不需要经典蓝牙）
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // 第3步：初始化蓝牙控制器（使用默认配置）
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s initialize controller failed", __func__);
        return;
    }

    // 第4步：启用蓝牙控制器（仅 BLE 模式）
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s enable controller failed", __func__);
        return;
    }

    // 第5步：初始化 Bluedroid（ESP-IDF 的蓝牙主机协议栈）
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed", __func__);
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed", __func__);
        return;
    }

    // 第6步：初始化 HID Device Profile
    if((ret = esp_hidd_profile_init()) != ESP_OK) {
        ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed", __func__);
    }

    // 第7步：注册 GAP 事件回调 和 HID 事件回调
    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    // 第8步：配置 BLE 安全参数（配对与加密）
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;            // 认证后绑定（Bonding）
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;                  // IO 能力：无输入无输出（键盘无显示屏）
    uint8_t key_size = 16;                                     // 加密密钥长度：16 字节
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;  // 期望对方分发的密钥类型
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;   // 本设备分发的密钥类型
    /**
     * init_key / rsp_key 含义说明：
     * - 作为 Slave：init_key = 期望 Master 分发的密钥, rsp_key = 本设备可分发給 Master 的密钥
     * - 作为 Master：rsp_key = 期望 Slave 分发的密钥, init_key = 本设备可分发給 Slave 的密钥
     */
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    // 第9步：初始化 Web Bluetooth 配置服务（浏览器通过 BLE 读写 NVS）
    web_ble_config_init();
    ble_cent_init();

    // 第10步：创建 Demo 任务（栈大小 2048 字节，优先级 5）
    xTaskCreate(&hid_demo_task, "hid_task", 2048, NULL, 5, NULL);
}
