/*
 * BLE Central 模块 — ESP32 作为蓝牙主机扫描/连接键盘鼠标
 */

#ifndef BLE_CENT_H__
#define BLE_CENT_H__

#include "esp_gap_ble_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化 BLE Central 模块（注册 GATT Client） */
void ble_cent_init(void);

/** @brief 开始扫描周围 BLE 设备，结果通过 web_ble_config_log 输出 */
void ble_cent_start_scan(void);

/** @brief 连接指定地址的 BLE 设备（地址格式 "aabbccddeeff"，addr_type: 0=public 1=random）*/
void ble_cent_connect(const char *addr_str, uint8_t addr_type);

/** @brief GAP 事件处理（在主程序 gap_event_handler 中调用）*/
void ble_cent_gap_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

#ifdef __cplusplus
}
#endif

#endif
