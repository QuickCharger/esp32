/*
 * 经典蓝牙 HID Host 模块 — 连接经典蓝牙键盘/鼠标
 *
 * ESP32 作为经典蓝牙 HID Host（主设备），发现、配对、连接苹果键盘等
 * 经典蓝牙 HID 设备，接收输入报告后转发给 BLE 连接的电脑。
 */

#ifndef BT_HID_HOST_H
#define BT_HID_HOST_H

#include "esp_gap_bt_api.h"

/**
 * @brief 初始化经典蓝牙 HID Host（注册回调 + 初始化 profile）
 */
void bt_hid_host_init(void);

/**
 * @brief 开始经典蓝牙设备发现（Inquiry）
 */
void bt_hid_host_start_discovery(void);

/**
 * @brief 连接指定地址的经典蓝牙 HID 设备
 *
 * @param addr_str 地址字符串，格式 "aabbccddeeff"（无冒号）
 */
void bt_hid_host_connect(const char *addr_str);

/**
 * @brief 经典蓝牙 GAP 事件回调（设备发现、配对）
 *
 * 由主程序的经典蓝牙 GAP 回调分发调用。
 */
void bt_hid_host_gap_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

#endif /* BT_HID_HOST_H */
