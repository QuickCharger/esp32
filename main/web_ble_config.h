/*
 * Web Bluetooth 配置服务 — 头文件
 */

#ifndef WEB_BLE_CONFIG_H__
#define WEB_BLE_CONFIG_H__

#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 Web Bluetooth 配置服务
 *
 * 注册一个自定义 GATT Service 和 Read/Write Characteristic，
 * 浏览器可通过 Web Bluetooth API 连接并读写数据。
 */
void web_ble_config_init(void);

/**
 * @brief GATT 事件处理入口
 *
 * 需要在主程序的 GATT 回调中调用此函数来分发事件。
 */
void web_ble_config_gatts_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                   esp_ble_gatts_cb_param_t *param);

#ifdef __cplusplus
}
#endif

#endif /* WEB_BLE_CONFIG_H__ */
