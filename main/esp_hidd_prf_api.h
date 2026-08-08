/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/**
 * @file esp_hidd_prf_api.h
 * @brief BLE HID Device (HIDD) Profile 对外 API 头文件
 *
 * 本文件定义了 HID 设备 Profile 的事件类型、回调参数结构体和 API 函数声明。
 * 应用层通过调用这些 API 来初始化 HID 设备、发送键盘/鼠标/消费者控制数据。
 */

#ifndef __ESP_HIDD_API_H__
#define __ESP_HIDD_API_H__

#include "esp_bt_defs.h"
#include "esp_gatt_defs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HID 设备回调事件类型枚举
 *
 * 当 HID Profile 状态发生变化时，会通过回调函数通知应用层
 */
typedef enum {
    ESP_HIDD_EVENT_REG_FINISH = 0,          /*!< HID Profile 注册完成 */
    ESP_BAT_EVENT_REG,                       /*!< 电池服务注册完成 */
    ESP_HIDD_EVENT_DEINIT_FINISH,            /*!< HID Profile 反初始化完成 */
    ESP_HIDD_EVENT_BLE_CONNECT,             /*!< BLE 连接建立 */
    ESP_HIDD_EVENT_BLE_DISCONNECT,          /*!< BLE 连接断开 */
    ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT,  /*!< 主机写入 Vendor 报告 */
    ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT,     /*!< 主机写入 LED 报告（键盘指示灯）*/
} esp_hidd_cb_event_t;

/// HID 连接状态
typedef enum {
    ESP_HIDD_STA_CONN_SUCCESS = 0x00,       /*!< 连接成功 */
    ESP_HIDD_STA_CONN_FAIL    = 0x01,       /*!< 连接失败 */
} esp_hidd_sta_conn_state_t;

/// HID 初始化状态
typedef enum {
    ESP_HIDD_INIT_OK = 0,                   /*!< 初始化成功 */
    ESP_HIDD_INIT_FAILED = 1,               /*!< 初始化失败 */
} esp_hidd_init_state_t;

/// HID 反初始化状态
typedef enum {
    ESP_HIDD_DEINIT_OK = 0,                 /*!< 反初始化成功 */
    ESP_HIDD_DEINIT_FAILED = 0,             /*!< 反初始化失败 */
} esp_hidd_deinit_state_t;

/**
 * @brief 键盘修饰键位掩码
 *
 * 对应 USB HID 键盘报告的第一个字节（修饰键字节），
 * 每一位代表一个修饰键的按下状态
 */
#define LEFT_CONTROL_KEY_MASK        (1 << 0)   /*!< 左 Ctrl  键掩码 */
#define LEFT_SHIFT_KEY_MASK          (1 << 1)   /*!< 左 Shift 键掩码 */
#define LEFT_ALT_KEY_MASK            (1 << 2)   /*!< 左 Alt   键掩码 */
#define LEFT_GUI_KEY_MASK            (1 << 3)   /*!< 左 GUI（Win/Cmd）键掩码 */
#define RIGHT_CONTROL_KEY_MASK       (1 << 4)   /*!< 右 Ctrl  键掩码 */
#define RIGHT_SHIFT_KEY_MASK         (1 << 5)   /*!< 右 Shift 键掩码 */
#define RIGHT_ALT_KEY_MASK           (1 << 6)   /*!< 右 Alt   键掩码 */
#define RIGHT_GUI_KEY_MASK           (1 << 7)   /*!< 右 GUI（Win/Cmd）键掩码 */

typedef uint8_t key_mask_t;  /*!< 键盘修饰键位掩码类型 */

/**
 * @brief HID 设备回调参数联合体
 *
 * 根据不同的事件类型，使用联合体中对应的结构体字段获取事件参数
 */
typedef union {
    /** ESP_HIDD_EVENT_REG_FINISH — HID 注册完成事件参数 */
    struct hidd_init_finish_evt_param {
        esp_hidd_init_state_t state;        /*!< 初始化状态（成功/失败） */
        esp_gatt_if_t gatts_if;             /*!< GATT 接口句柄 */
    } init_finish;

    /** ESP_HIDD_EVENT_DEINIT_FINISH — 反初始化完成事件参数 */
    struct hidd_deinit_finish_evt_param {
        esp_hidd_deinit_state_t state;      /*!< 反初始化状态 */
    } deinit_finish;

    /** ESP_HIDD_EVENT_BLE_CONNECT — BLE 连接事件参数 */
    struct hidd_connect_evt_param {
        uint16_t conn_id;                   /*!< 连接 ID */
        esp_bd_addr_t remote_bda;           /*!< 远程设备蓝牙地址 */
    } connect;

    /** ESP_HIDD_EVENT_BLE_DISCONNECT — BLE 断开事件参数 */
    struct hidd_disconnect_evt_param {
        uint16_t conn_id;                   /*!< 断开连接的 conn_id（多连接模式用于区分哪台设备断开）*/
        esp_bd_addr_t remote_bda;           /*!< 远程设备蓝牙地址 */
    } disconnect;

    /** ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT — Vendor 报告写入事件参数 */
    struct hidd_vendor_write_evt_param {
        uint16_t conn_id;                   /*!< 连接 ID */
        uint16_t report_id;                 /*!< 报告 ID */
        uint16_t length;                    /*!< 数据长度 */
        uint8_t  *data;                     /*!< 数据指针 */
    } vendor_write;

    /** ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT — LED 报告写入事件参数 */
    struct hidd_led_write_evt_param {
        uint16_t conn_id;                   /*!< 连接 ID */
        uint8_t report_id;                  /*!< 报告 ID */
        uint8_t length;                     /*!< 数据长度 */
        uint8_t *data;                      /*!< 数据指针（键盘 LED 状态：NumLock/CapsLock/ScrollLock 等） */
    } led_write;
} esp_hidd_cb_param_t;


/**
 * @brief HID 设备事件回调函数类型
 * @param event  事件类型，参考 esp_hidd_cb_event_t
 * @param param  事件参数，根据事件类型取联合体中对应字段
 */
typedef void (*esp_hidd_event_cb_t) (esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);


/* =====================================================================
 *                          API 函数声明
 * ===================================================================== */

/**
 * @brief   注册 HID 设备回调函数
 *
 *          调用后，HID Profile 相关事件将通过回调函数通知应用层。
 *          内部会注册 GATT 应用并初始化 Profile。
 *
 * @param[in] callbacks  回调函数指针
 * @return   ESP_OK 成功，其他值失败
 */
esp_err_t esp_hidd_register_callbacks(esp_hidd_event_cb_t callbacks);

/**
 * @brief   初始化 HID Device Profile
 *
 *          必须在蓝牙协议栈初始化完成后调用。
 *          会重置 HID 环境变量并标记 Profile 已启用。
 *
 * @return   ESP_OK 成功，其他值失败
 */
esp_err_t esp_hidd_profile_init(void);

/**
 * @brief   反初始化 HID Device Profile
 *
 *          停止并删除 HID 服务，注销 GATT 应用。
 *
 * @return   ESP_OK 成功，其他值失败
 */
esp_err_t esp_hidd_profile_deinit(void);

/**
 * @brief   获取 HID Profile 版本号
 *
 * @return   高 8 位为主版本号，低 8 位为次版本号
 */
uint16_t esp_hidd_get_version(void);

/**
 * @brief   发送消费者控制报告（Consumer Control）
 *
 *          用于发送媒体控制命令，如：音量+/-、播放/暂停、静音等
 *
 * @param[in] conn_id     连接 ID
 * @param[in] key_cmd     消费者控制命令码（参考 hid_dev.h 中的 HID_CONSUMER_* 定义）
 * @param[in] key_pressed true=按键按下，false=按键释放
 */
void esp_hidd_send_consumer_value(uint16_t conn_id, uint8_t key_cmd, bool key_pressed);

/**
 * @brief   发送键盘报告
 *
 * @param[in] conn_id            连接 ID
 * @param[in] special_key_mask   修饰键掩码（Ctrl/Shift/Alt/GUI 的组合）
 * @param[in] keyboard_cmd       按键码数组（每个元素为 HID_KEY_* 定义的键值）
 * @param[in] num_key            按键数量（最多 6 个）
 */
void esp_hidd_send_keyboard_value(uint16_t conn_id, key_mask_t special_key_mask, uint8_t *keyboard_cmd, uint8_t num_key);

/**
 * @brief   发送鼠标报告
 *
 * @param[in] conn_id       连接 ID
 * @param[in] mouse_button  鼠标按键状态（左/中/右键）
 * @param[in] mickeys_x     X 轴移动量（有符号，正值向右）
 * @param[in] mickeys_y     Y 轴移动量（有符号，正值向上）
 */
void esp_hidd_send_mouse_value(uint16_t conn_id, uint8_t mouse_button, int8_t mickeys_x, int8_t mickeys_y);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_HIDD_API_H__ */
