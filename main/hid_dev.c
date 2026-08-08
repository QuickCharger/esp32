/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/**
 * @file hid_dev.c
 * @brief HID 设备层实现
 *
 * 本文件实现了 HID 报告发送和消费者控制报告构建功能：
 * - 报告映射表管理（注册和查找）
 * - 通过 GATT Indication 发送 HID 报告
 * - 构建 Consumer Control 报告（根据命令类型设置 2 字节缓冲区中的位域）
 */

#include "hid_dev.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_log.h"

static hid_report_map_t *hid_dev_rpt_tbl;       /*!< 报告映射表指针 */
static uint8_t hid_dev_rpt_tbl_Len;              /*!< 报告映射表条目数 */

/**
 * @brief 根据报告 ID、类型和协议模式查找报告映射条目
 *
 * @param id    报告 ID
 * @param type  报告类型（Input/Output/Feature）
 * @return      找到的映射条目指针，未找到返回 NULL
 */
static hid_report_map_t *hid_dev_rpt_by_id(uint8_t id, uint8_t type)
{
    hid_report_map_t *rpt = hid_dev_rpt_tbl;

    for (uint8_t i = hid_dev_rpt_tbl_Len; i > 0; i--, rpt++) {
        if (rpt->id == id && rpt->type == type && rpt->mode == hidProtocolMode) {
            return rpt;    // 找到匹配的报告映射
        }
    }

    return NULL;           // 未找到
}

/**
 * @brief 注册 HID 报告映射表
 *
 * 将 Profile 层建立的报告 ID → GATT 句柄映射表注册到设备层，
 * 后续发送报告时通过此表查找目标 Characteristic。
 */
void hid_dev_register_reports(uint8_t num_reports, hid_report_map_t *p_report)
{
    hid_dev_rpt_tbl = p_report;
    hid_dev_rpt_tbl_Len = num_reports;
    return;
}

/**
 * @brief 发送 HID 报告
 *
 * 执行流程：
 * 1. 通过 hid_dev_rpt_by_id() 查找目标 GATT Characteristic 句柄
 * 2. 调用 esp_ble_gatts_send_indicate() 通过 GATT Indication 发送数据
 *
 * @note 使用 Indication（而非 Notification），因为 Indication 需要主机确认，
 *       保证数据可靠送达。
 */
void hid_dev_send_report(esp_gatt_if_t gatts_if, uint16_t conn_id,
                                    uint8_t id, uint8_t type, uint8_t length, uint8_t *data)
{
    hid_report_map_t *p_rpt;

    // 根据报告 ID 和类型查找 GATT 句柄
    if ((p_rpt = hid_dev_rpt_by_id(id, type)) != NULL) {
        // 通过 GATT Indication 发送报告数据
        ESP_LOGD(HID_LE_PRF_TAG, "%s(), send the report, handle = %d", __func__, p_rpt->handle);
        esp_ble_gatts_send_indicate(gatts_if, conn_id, p_rpt->handle, length, data, false);
    }

    return;
}

/**
 * @brief 构建消费者控制（Consumer Control）报告
 *
 * Consumer Control 报告为 2 字节格式：
 *   Byte 0: [Channel(bit4-5)] [Volume Up(bit6)] [Volume Down(bit7)] [Numeric(bit0-3)]
 *   Byte 1: [Selection(bit4-5)] [Button(bit0-3)]
 *
 * 根据传入的命令类型，使用对应的宏设置缓冲区中的相应位域。
 *
 * @param buffer  2 字节报告缓冲区（调用方负责分配和清零）
 * @param cmd     消费者控制命令（HID_CONSUMER_* 系列）
 */
void hid_consumer_build_report(uint8_t *buffer, consumer_cmd_t cmd)
{
    if (!buffer) {
        ESP_LOGE(HID_LE_PRF_TAG, "%s(), the buffer is NULL, hid build report failed.", __func__);
        return;
    }

    switch (cmd) {
        case HID_CONSUMER_CHANNEL_UP:
            HID_CC_RPT_SET_CHANNEL(buffer, HID_CC_RPT_CHANNEL_UP);     // 频道+
            break;

        case HID_CONSUMER_CHANNEL_DOWN:
            HID_CC_RPT_SET_CHANNEL(buffer, HID_CC_RPT_CHANNEL_DOWN);   // 频道-
            break;

        case HID_CONSUMER_VOLUME_UP:
            HID_CC_RPT_SET_VOLUME_UP(buffer);                          // 音量+
            break;

        case HID_CONSUMER_VOLUME_DOWN:
            HID_CC_RPT_SET_VOLUME_DOWN(buffer);                        // 音量-
            break;

        case HID_CONSUMER_MUTE:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_MUTE);            // 静音
            break;

        case HID_CONSUMER_POWER:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_POWER);           // 电源
            break;

        case HID_CONSUMER_RECALL_LAST:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_LAST);            // 上一个
            break;

        case HID_CONSUMER_ASSIGN_SEL:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_ASSIGN_SEL);      // 分配选择
            break;

        case HID_CONSUMER_PLAY:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_PLAY);            // 播放
            break;

        case HID_CONSUMER_PAUSE:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_PAUSE);           // 暂停
            break;

        case HID_CONSUMER_RECORD:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_RECORD);          // 录制
            break;

        case HID_CONSUMER_FAST_FORWARD:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_FAST_FWD);        // 快进
            break;

        case HID_CONSUMER_REWIND:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_REWIND);          // 快退
            break;

        case HID_CONSUMER_SCAN_NEXT_TRK:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_SCAN_NEXT_TRK);   // 下一曲
            break;

        case HID_CONSUMER_SCAN_PREV_TRK:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_SCAN_PREV_TRK);   // 上一曲
            break;

        case HID_CONSUMER_STOP:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_STOP);            // 停止
            break;

        default:
            break;
    }

    return;
}
