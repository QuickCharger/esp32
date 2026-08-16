/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/**
 * @file esp_hidd_prf_api.c
 * @brief BLE HID Device Profile 对外 API 实现
 *
 * 本文件实现了 HID Profile 对应用层暴露的 API 函数：
 * - 初始化和反初始化 Profile
 * - 发送键盘/鼠标/消费者控制报告
 * - 注册事件回调
 */

#include "esp_hidd_prf_api.h"
#include "hidd_le_prf_int.h"
#include "hid_dev.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

// 各类型 HID 报告的长度定义（根据 Report Map 描述符确定）
#define HID_KEYBOARD_IN_RPT_LEN     8     /*!< 键盘输入报告长度：1字节修饰键 + 1字节保留 + 6字节按键码 */
#define HID_LED_OUT_RPT_LEN         1     /*!< LED 输出报告长度：1字节 LED 状态 */
#define HID_MOUSE_IN_RPT_LEN        5     /*!< 鼠标输入报告长度：1字节按键 + 1字节X + 1字节Y + 1字节滚轮 + 1字节AC Pan */
#define HID_CC_IN_RPT_LEN           2     /*!< 消费者控制输入报告长度：2字节 */

/* =====================================================================
 *                     API 函数实现
 * ===================================================================== */

/**
 * @brief 注册 HID 设备回调函数
 *
 * 执行流程：
 * 1. 保存用户回调函数到全局环境变量
 * 2. 注册 GATT 事件回调（hidd_register_cb）
 * 3. 注册电池服务 GATT 应用（BATTRAY_APP_ID = 0x180f）
 * 4. 注册 HID 服务 GATT 应用（HIDD_APP_ID = 0x1812）
 *    注册成功后 ESP-IDF 会触发 ESP_GATTS_REG_EVT 事件，
 *    在事件处理中创建 GATT 属性表
 */
esp_err_t esp_hidd_register_callbacks(esp_hidd_event_cb_t callbacks)
{
    esp_err_t hidd_status;

    if(callbacks != NULL) {
   	    hidd_le_env.hidd_cb = callbacks;          // 保存用户回调函数
    } else {
        return ESP_FAIL;
    }

    if((hidd_status = hidd_register_cb()) != ESP_OK) {  // 注册 GATT 事件回调
        return hidd_status;
    }

    esp_ble_gatts_app_register(BATTRAY_APP_ID);          // 注册电池服务应用

    if((hidd_status = esp_ble_gatts_app_register(HIDD_APP_ID)) != ESP_OK) {  // 注册 HID 服务应用
        return hidd_status;
    }

    return hidd_status;
}

/**
 * @brief 初始化 HID Device Profile
 *
 * 重置全局环境变量（hidd_le_env）并标记 Profile 已启用。
 * 必须在蓝牙协议栈初始化之后、注册回调之前调用。
 */
esp_err_t esp_hidd_profile_init(void)
{
     if (hidd_le_env.enabled) {
        ESP_LOGE(HID_LE_PRF_TAG, "HID device profile already initialized");
        return ESP_FAIL;
    }
    // 重置 HID 设备环境变量
    memset(&hidd_le_env, 0, sizeof(hidd_le_env_t));
    hidd_le_env.enabled = true;
    return ESP_OK;
}

/**
 * @brief 反初始化 HID Device Profile
 *
 * 停止并删除 HID 服务，然后注销 GATT 应用。
 */
esp_err_t esp_hidd_profile_deinit(void)
{
    uint16_t hidd_svc_hdl = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC];
    if (!hidd_le_env.enabled) {
        ESP_LOGE(HID_LE_PRF_TAG, "HID device profile already initialized");
        return ESP_OK;
    }

    if(hidd_svc_hdl != 0) {
	esp_ble_gatts_stop_service(hidd_svc_hdl);        // 停止 HID 服务
	esp_ble_gatts_delete_service(hidd_svc_hdl);      // 删除 HID 服务
    } else {
	return ESP_FAIL;
   }

    /* 从 BTA_GATTS 模块注销 HID device profile */
    esp_ble_gatts_app_unregister(hidd_le_env.gatt_if);

    return ESP_OK;
}

/**
 * @brief 获取 HID Profile 版本号
 */
uint16_t esp_hidd_get_version(void)
{
	return HIDD_VERSION;
}

/**
 * @brief 发送消费者控制（Consumer Control）报告
 *
 * 消费者控制报告的格式为 2 字节：
 * - Byte 0: 频道(bit4-5) + 音量+(bit6) + 音量-(bit7) + 数字键区(bit0-3)
 * - Byte 1: 选择(bit4-5) + 按钮(bit0-3)
 *
 * @param conn_id     连接 ID
 * @param key_cmd     命令码（如 HID_CONSUMER_VOLUME_UP）
 * @param key_pressed true=按下（填充报告数据），false=释放（发送全零报告）
 */
void esp_hidd_send_consumer_value(uint16_t conn_id, uint8_t key_cmd, bool key_pressed)
{
    uint8_t buffer[HID_CC_IN_RPT_LEN] = {0, 0};          // 初始化为全零（释放状态）
    if (key_pressed) {
        ESP_LOGD(HID_LE_PRF_TAG, "hid_consumer_build_report");
        hid_consumer_build_report(buffer, key_cmd);      // 按键按下时填充报告
    }                                                     // 释放时保持全零
    ESP_LOGD(HID_LE_PRF_TAG, "buffer[0] = %x, buffer[1] = %x", buffer[0], buffer[1]);
    hid_dev_send_report(hidd_le_env.gatt_if, conn_id,
                        HID_RPT_ID_CC_IN, HID_REPORT_TYPE_INPUT, HID_CC_IN_RPT_LEN, buffer);
    return;
}

/**
 * @brief 发送键盘报告
 *
 * 键盘输入报告格式（8 字节）：
 * - Byte 0: 修饰键位掩码（Ctrl/Shift/Alt/GUI）
 * - Byte 1: 保留字节（恒为 0）
 * - Byte 2-7: 最多 6 个按键码
 *
 * @param conn_id           连接 ID
 * @param special_key_mask  修饰键掩码
 * @param keyboard_cmd      按键码数组
 * @param num_key           按键数量（最多 6 个）
 */
void esp_hidd_send_keyboard_value(uint16_t conn_id, key_mask_t special_key_mask, uint8_t *keyboard_cmd, uint8_t num_key)
{
    if (num_key > HID_KEYBOARD_IN_RPT_LEN - 2) {
        ESP_LOGE(HID_LE_PRF_TAG, "%s(), the number key should not be more than %d", __func__, HID_KEYBOARD_IN_RPT_LEN);
        return;
    }

    uint8_t buffer[HID_KEYBOARD_IN_RPT_LEN] = {0};

    buffer[0] = special_key_mask;                        // Byte 0: 修饰键掩码

    for (int i = 0; i < num_key; i++) {
        buffer[i+2] = keyboard_cmd[i];                   // Byte 2-7: 按键码
    }

    ESP_LOGD(HID_LE_PRF_TAG, "the key vaule = %d,%d,%d, %d, %d, %d,%d, %d", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
    hid_dev_send_report(hidd_le_env.gatt_if, conn_id,
                        HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT, HID_KEYBOARD_IN_RPT_LEN, buffer);
    return;
}

/**
 * @brief 发送鼠标报告
 *
 * 鼠标输入报告格式（5 字节）：
 * - Byte 0: 按键状态位图（bit0=左键 bit1=右键 bit2=中键 bit3~7=扩展按键）
 * - Byte 1: X 轴移动量（有符号，正值向右）
 * - Byte 2: Y 轴移动量（有符号，正值向上）
 * - Byte 3: 垂直滚轮 Wheel（有符号，正值向上）
 * - Byte 4: 水平滚轮 AC Pan（有符号，正值向右）
 *
 * @param conn_id       连接 ID
 * @param mouse_button  按键状态位图
 * @param mickeys_x     X 轴移动
 * @param mickeys_y     Y 轴移动
 * @param wheel         垂直滚轮
 * @param ac_pan        水平滚轮
 */
void esp_hidd_send_mouse_value(uint16_t conn_id, uint8_t mouse_button, int8_t mickeys_x, int8_t mickeys_y, int8_t wheel, int8_t ac_pan)
{
    uint8_t buffer[HID_MOUSE_IN_RPT_LEN];

    buffer[0] = mouse_button;    // Byte 0: 按键
    buffer[1] = mickeys_x;       // Byte 1: X 移动
    buffer[2] = mickeys_y;       // Byte 2: Y 移动
    buffer[3] = wheel;           // Byte 3: 垂直滚轮
    buffer[4] = ac_pan;          // Byte 4: 水平滚轮

    hid_dev_send_report(hidd_le_env.gatt_if, conn_id,
                        HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT, HID_MOUSE_IN_RPT_LEN, buffer);
    return;
}
