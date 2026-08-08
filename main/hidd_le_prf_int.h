/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/**
 * @file hidd_le_prf_int.h
 * @brief BLE HID Device Profile 内部头文件
 *
 * 本文件定义了 HID Profile 内部使用的：
 * - 常量定义（报告ID、属性长度、协议模式等）
 * - GATT 属性表索引枚举
 * - 内部数据结构（环境变量、连接控制块、报告映射表等）
 * - 内部函数声明
 */

#ifndef __HID_DEVICE_LE_PRF__
#define __HID_DEVICE_LE_PRF__
#include <stdbool.h>
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_hidd_prf_api.h"
#include "esp_gap_ble_api.h"
#include "hid_dev.h"

/* =====================================================================
 *                    编译选项与日志标签
 * ===================================================================== */

/// 是否支持 Vendor 自定义报告（Win10 不支持，默认关闭）
#define SUPPORT_REPORT_VENDOR                 false

/// HID BLE Profile 日志标签
#define HID_LE_PRF_TAG                        "HID_LE_PRF"

/* =====================================================================
 *                    HID 实例与报告数量定义
 * ===================================================================== */

/// 最大 HID 服务实例数（可由外部宏 USE_ONE_HIDS_INSTANCE 控制）
#ifndef USE_ONE_HIDS_INSTANCE
#define HIDD_LE_NB_HIDS_INST_MAX              (2)   /*!< 默认 2 个 HID 服务实例 */
#else
#define HIDD_LE_NB_HIDS_INST_MAX              (1)   /*!< 仅 1 个 HID 服务实例 */
#endif

/// HID Profile 版本号
#define HIDD_GREAT_VER   0x01                     /*!< 主版本号 */
#define HIDD_SUB_VER     0x00                     /*!< 次版本号 */
#define HIDD_VERSION     ((HIDD_GREAT_VER<<8)|HIDD_SUB_VER)  /*!< 完整版本号（高8位主版本，低8位次版本）*/

/// 最大同时连接的应用数
#define HID_MAX_APPS                 1

/// HID 服务中定义的报告数量（鼠标输入 + 键盘输入 + CC输入 + LED输出 + Boot键盘输入 + Boot键盘输出 + Boot鼠标输入 + Feature + Vendor）
#define HID_NUM_REPORTS          9

/* =====================================================================
 *                    HID 报告 ID 定义
 * ===================================================================== */

/// BLE HID 服务中各个报告的 ID，在 Report Reference Descriptor 中使用
#define HID_RPT_ID_MOUSE_IN      1          /*!< 鼠标输入报告 ID */
#define HID_RPT_ID_KEY_IN        2          /*!< 键盘输入报告 ID */
#define HID_RPT_ID_CC_IN         3          /*!< 消费者控制（Consumer Control）输入报告 ID */
#define HID_RPT_ID_VENDOR_OUT    4          /*!< Vendor 自定义输出报告 ID */
#define HID_RPT_ID_LED_OUT       2          /*!< LED 输出报告 ID（键盘指示灯：NumLock/CapsLock 等）*/
#define HID_RPT_ID_FEATURE       0          /*!< Feature 报告 ID */

/// GATT 应用 ID
#define HIDD_APP_ID			0x1812  /*!< HID 服务的 GATT 应用 ID（ATT_SVC_HID）*/

#define BATTRAY_APP_ID       0x180f      /*!< 电池服务的 GATT 应用 ID */

/// 蓝牙 SIG 定义的 HID 服务 UUID（16-bit）
#define ATT_SVC_HID          0x1812

/* =====================================================================
 *                    属性长度限制定义
 * ===================================================================== */

/// 每个 HID 服务最多可添加的报告 Characteristic 数量（最多 11 个）
#define HIDD_LE_NB_REPORT_INST_MAX            (5)

/// 报告 Characteristic 值的最大长度（255 字节）
#define HIDD_LE_REPORT_MAX_LEN                (255)
/// 报告映射（Report Map）Characteristic 值的最大长度（512 字节）
#define HIDD_LE_REPORT_MAP_MAX_LEN            (512)

/// Boot Report Characteristic 值的最大长度（8 字节）
#define HIDD_LE_BOOT_REPORT_MAX_LEN           (8)

/// Boot 键盘输入报告通知配置位掩码
#define HIDD_LE_BOOT_KB_IN_NTF_CFG_MASK       (0x40)
/// Boot 鼠标输入报告通知配置位掩码
#define HIDD_LE_BOOT_MOUSE_IN_NTF_CFG_MASK    (0x80)
/// Report 通知配置位掩码
#define HIDD_LE_REPORT_NTF_CFG_MASK           (0x20)


/* =====================================================================
 *                    HID 协议相关常量
 * ===================================================================== */

/* HID Information 标志位 */
#define HID_FLAGS_REMOTE_WAKE           0x01      /*!< 支持远程唤醒 */
#define HID_FLAGS_NORMALLY_CONNECTABLE  0x02      /*!< 通常可连接 */

/* HID Control Point 命令 */
#define HID_CMD_SUSPEND                 0x00      /*!< 挂起（Suspend）*/
#define HID_CMD_EXIT_SUSPEND            0x01      /*!< 退出挂起（Exit Suspend）*/

/* HID 协议模式 */
#define HID_PROTOCOL_MODE_BOOT          0x00      /*!< Boot 协议模式（简单固定格式）*/
#define HID_PROTOCOL_MODE_REPORT        0x01      /*!< Report 协议模式（使用 Report Map 定义格式）*/

/* 属性值长度 */
#define HID_PROTOCOL_MODE_LEN           1         /*!< HID Protocol Mode 属性长度 */
#define HID_INFORMATION_LEN             4         /*!< HID Information 属性长度（bcdHID + bCountryCode + Flags）*/
#define HID_REPORT_REF_LEN              2         /*!< HID Report Reference Descriptor 长度 */
#define HID_EXT_REPORT_REF_LEN          2         /*!< External Report Reference Descriptor 长度 */

/// HID 键盘特性标志（支持远程唤醒）
#define HID_KBD_FLAGS             HID_FLAGS_REMOTE_WAKE

/* HID 报告类型（对应 USB HID 规范）*/
#define HID_REPORT_TYPE_INPUT       1             /*!< 输入报告（设备 → 主机）*/
#define HID_REPORT_TYPE_OUTPUT      2             /*!< 输出报告（主机 → 设备）*/
#define HID_REPORT_TYPE_FEATURE     3             /*!< Feature 报告（双向）*/


/* =====================================================================
 *                  GATT 属性表索引枚举
 *
 * 以下枚举定义了 HID 服务 GATT 属性表中每个属性的索引位置。
 * 在属性表创建后，实际 GATT 句柄会存储在 att_tbl[] 数组中。
 * ===================================================================== */

/// HID 服务 GATT 属性表索引（对应 hidd_le_gatt_db[] 数组）
enum {
    HIDD_LE_IDX_SVC,                       /*!< HID 服务声明 */

    // Included Service（引用电池服务）
    HIDD_LE_IDX_INCL_SVC,                  /*!< Include 服务声明 */

    // HID Information（HID 设备信息）
    HIDD_LE_IDX_HID_INFO_CHAR,             /*!< HID Information Characteristic 声明 */
    HIDD_LE_IDX_HID_INFO_VAL,              /*!< HID Information 值 */

    // HID Control Point（主机通过此控制点管理设备）
    HIDD_LE_IDX_HID_CTNL_PT_CHAR,          /*!< HID Control Point Characteristic 声明 */
    HIDD_LE_IDX_HID_CTNL_PT_VAL,           /*!< HID Control Point 值 */

    // Report Map（描述 HID 报告格式）
    HIDD_LE_IDX_REPORT_MAP_CHAR,           /*!< Report Map Characteristic 声明 */
    HIDD_LE_IDX_REPORT_MAP_VAL,            /*!< Report Map 值 */
    HIDD_LE_IDX_REPORT_MAP_EXT_REP_REF,    /*!< External Report Reference 描述符 */

    // Protocol Mode（Boot 模式 / Report 模式）
    HIDD_LE_IDX_PROTO_MODE_CHAR,           /*!< Protocol Mode Characteristic 声明 */
    HIDD_LE_IDX_PROTO_MODE_VAL,            /*!< Protocol Mode 值 */

    // 鼠标输入报告
    HIDD_LE_IDX_REPORT_MOUSE_IN_CHAR,      /*!< 鼠标输入 Characteristic 声明 */
    HIDD_LE_IDX_REPORT_MOUSE_IN_VAL,       /*!< 鼠标输入值 */
    HIDD_LE_IDX_REPORT_MOUSE_IN_CCC,       /*!< 鼠标输入 CCCD（Client Characteristic Configuration Descriptor）*/
    HIDD_LE_IDX_REPORT_MOUSE_REP_REF,      /*!< 鼠标输入 Report Reference 描述符 */

    // 键盘输入报告
    HIDD_LE_IDX_REPORT_KEY_IN_CHAR,        /*!< 键盘输入 Characteristic 声明 */
    HIDD_LE_IDX_REPORT_KEY_IN_VAL,         /*!< 键盘输入值 */
    HIDD_LE_IDX_REPORT_KEY_IN_CCC,         /*!< 键盘输入 CCCD */
    HIDD_LE_IDX_REPORT_KEY_IN_REP_REF,     /*!< 键盘输入 Report Reference 描述符 */

    // LED 输出报告（键盘指示灯）
    HIDD_LE_IDX_REPORT_LED_OUT_CHAR,       /*!< LED 输出 Characteristic 声明 */
    HIDD_LE_IDX_REPORT_LED_OUT_VAL,        /*!< LED 输出值 */
    HIDD_LE_IDX_REPORT_LED_OUT_REP_REF,    /*!< LED 输出 Report Reference 描述符 */

#if (SUPPORT_REPORT_VENDOR  == true)
    /// Vendor 自定义报告（Win10 不支持，默认关闭）
    HIDD_LE_IDX_REPORT_VENDOR_OUT_CHAR,
    HIDD_LE_IDX_REPORT_VENDOR_OUT_VAL,
    HIDD_LE_IDX_REPORT_VENDOR_OUT_REP_REF,
#endif

    // 消费者控制（Consumer Control）输入报告
    HIDD_LE_IDX_REPORT_CC_IN_CHAR,         /*!< Consumer Control Characteristic 声明 */
    HIDD_LE_IDX_REPORT_CC_IN_VAL,          /*!< Consumer Control 值 */
    HIDD_LE_IDX_REPORT_CC_IN_CCC,          /*!< Consumer Control CCCD */
    HIDD_LE_IDX_REPORT_CC_IN_REP_REF,      /*!< Consumer Control Report Reference 描述符 */

    // Boot 键盘输入报告（兼容 BIOS 等简单系统）
    HIDD_LE_IDX_BOOT_KB_IN_REPORT_CHAR,    /*!< Boot 键盘输入 Characteristic 声明 */
    HIDD_LE_IDX_BOOT_KB_IN_REPORT_VAL,     /*!< Boot 键盘输入值 */
    HIDD_LE_IDX_BOOT_KB_IN_REPORT_NTF_CFG, /*!< Boot 键盘输入 CCCD */

    // Boot 键盘输出报告
    HIDD_LE_IDX_BOOT_KB_OUT_REPORT_CHAR,   /*!< Boot 键盘输出 Characteristic 声明 */
    HIDD_LE_IDX_BOOT_KB_OUT_REPORT_VAL,    /*!< Boot 键盘输出值 */

    // Boot 鼠标输入报告
    HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_CHAR, /*!< Boot 鼠标输入 Characteristic 声明 */
    HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_VAL,  /*!< Boot 鼠标输入值 */
    HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_NTF_CFG, /*!< Boot 鼠标输入 CCCD */

    // Feature 报告
    HIDD_LE_IDX_REPORT_CHAR,               /*!< Feature Report Characteristic 声明 */
    HIDD_LE_IDX_REPORT_VAL,                /*!< Feature Report 值 */
    HIDD_LE_IDX_REPORT_REP_REF,            /*!< Feature Report Reference 描述符 */

    HIDD_LE_IDX_NB,                        /*!< 属性表总条目数 */
};


/// Characteristic 属性表索引（简化版，用于事件处理时的 Characteristic 识别）
enum {
    HIDD_LE_INFO_CHAR,                     /*!< HID Information */
    HIDD_LE_CTNL_PT_CHAR,                  /*!< HID Control Point */
    HIDD_LE_REPORT_MAP_CHAR,               /*!< Report Map */
    HIDD_LE_REPORT_CHAR,                   /*!< Report */
    HIDD_LE_PROTO_MODE_CHAR,               /*!< Protocol Mode */
    HIDD_LE_BOOT_KB_IN_REPORT_CHAR,        /*!< Boot Keyboard Input */
    HIDD_LE_BOOT_KB_OUT_REPORT_CHAR,       /*!< Boot Keyboard Output */
    HIDD_LE_BOOT_MOUSE_IN_REPORT_CHAR,     /*!< Boot Mouse Input */
    HIDD_LE_CHAR_MAX                       /*!< Characteristic 总数 */
};

/// Attribute 读取事件表索引
enum {
    HIDD_LE_READ_INFO_EVT,                 /*!< 读取 HID Information */
    HIDD_LE_READ_CTNL_PT_EVT,              /*!< 读取 Control Point */
    HIDD_LE_READ_REPORT_MAP_EVT,           /*!< 读取 Report Map */
    HIDD_LE_READ_REPORT_EVT,               /*!< 读取 Report */
    HIDD_LE_READ_PROTO_MODE_EVT,           /*!< 读取 Protocol Mode */
    HIDD_LE_BOOT_KB_IN_REPORT_EVT,         /*!< 读取 Boot Keyboard Input */
    HIDD_LE_BOOT_KB_OUT_REPORT_EVT,        /*!< 读取 Boot Keyboard Output */
    HIDD_LE_BOOT_MOUSE_IN_REPORT_EVT,      /*!< 读取 Boot Mouse Input */

    HID_LE_EVT_MAX                         /*!< 事件总数 */
};

/// Client Characteristic Configuration（CCCD）编码
enum {
    HIDD_LE_DESC_MASK = 0x10,              /*!< 描述符掩码 */

    HIDD_LE_BOOT_KB_IN_REPORT_CFG     = HIDD_LE_BOOT_KB_IN_REPORT_CHAR | HIDD_LE_DESC_MASK,
    HIDD_LE_BOOT_MOUSE_IN_REPORT_CFG  = HIDD_LE_BOOT_MOUSE_IN_REPORT_CHAR | HIDD_LE_DESC_MASK,
    HIDD_LE_REPORT_CFG                = HIDD_LE_REPORT_CHAR | HIDD_LE_DESC_MASK,
};

/// HID 设备特性标志
enum {
    HIDD_LE_CFG_KEYBOARD      = 0x01,      /*!< 支持键盘 */
    HIDD_LE_CFG_MOUSE         = 0x02,      /*!< 支持鼠标 */
    HIDD_LE_CFG_PROTO_MODE    = 0x04,      /*!< 支持协议模式切换 */
    HIDD_LE_CFG_MAP_EXT_REF   = 0x08,      /*!< 支持外部 Report Reference */
    HIDD_LE_CFG_BOOT_KB_WR    = 0x10,      /*!< Boot 键盘输出可写 */
    HIDD_LE_CFG_BOOT_MOUSE_WR = 0x20,      /*!< Boot 鼠标输出可写 */
};

/// Report Characteristic 配置标志
enum {
    HIDD_LE_CFG_REPORT_IN     = 0x01,      /*!< 输入报告 */
    HIDD_LE_CFG_REPORT_OUT    = 0x02,      /*!< 输出报告 */
    HIDD_LE_CFG_REPORT_FEAT   = 0x03,      /*!< Feature 报告（可作为掩码检查报告类型）*/
    HIDD_LE_CFG_REPORT_WR     = 0x10,      /*!< 报告可写 */
};

/// 连接清理函数指针（当前未使用）
#define HIDD_LE_CLEANUP_FNCT        (NULL)

/* =====================================================================
 *                        数据类型定义
 * ===================================================================== */

/**
 * @brief HID 设备特性结构体
 *
 * 描述 HID 服务的功能配置
 */
typedef struct {
    uint8_t svc_features;                              /*!< 服务特性标志 */
    uint8_t report_nb;                                 /*!< 数据库中 Report Characteristic 实例数 */
    uint8_t report_char_cfg[HIDD_LE_NB_REPORT_INST_MAX]; /*!< 每个 Report Characteristic 的配置 */
} hidd_feature_t;


/**
 * @brief HID 连接控制块（Connection Link Control Block）
 *
 * 管理每个 BLE 连接的状态信息
 */
typedef struct {
    bool                        in_use;        /*!< 此控制块是否正在使用 */
    bool                        congest;       /*!< 连接是否拥塞 */
    uint16_t                  conn_id;         /*!< GATT 连接 ID */
    bool                        connected;     /*!< 是否已连接 */
    esp_bd_addr_t         remote_bda;          /*!< 远程设备蓝牙地址 */
    uint32_t                  trans_id;        /*!< 传输 ID */
    uint8_t                    cur_srvc_id;    /*!< 当前服务 ID */
} hidd_clcb_t;

/**
 * @brief HID 报告映射表条目
 *
 * 将报告 ID 和类型映射到对应的 GATT 句柄
 */
typedef struct {
    uint16_t    handle;           /*!< Report Characteristic 的 GATT 句柄 */
    uint16_t    cccdHandle;       /*!< CCCD 的 GATT 句柄 */
    uint8_t     id;               /*!< 报告 ID */
    uint8_t     type;             /*!< 报告类型（Input/Output/Feature）*/
    uint8_t     mode;             /*!< 协议模式（Boot 或 Report）*/
} hidRptMap_t;


/**
 * @brief HID 设备实例结构体
 *
 * 管理 HID Profile 实例的状态
 */
typedef struct {
    uint8_t app_id;                                      /*!< Profile 应用 ID */
    uint16_t ntf_handle;                                 /*!< 通知句柄 */
    uint16_t att_tbl[HIDD_LE_IDX_NB];                    /*!< GATT 属性句柄表 */
    hidd_feature_t   hidd_feature[HIDD_LE_NB_HIDS_INST_MAX]; /*!< 支持的 HID 特性 */
    uint8_t proto_mode[HIDD_LE_NB_HIDS_INST_MAX];        /*!< 当前协议模式（Boot/Report）*/
    uint8_t hids_nb;                                     /*!< 数据库中已添加的 HID 服务数 */
    uint8_t pending_evt;                                 /*!< 待处理事件 */
    uint16_t pending_hal;                                /*!< 待处理句柄 */
} hidd_inst_t;

/**
 * @brief HID Report Reference 描述符结构
 *
 * 对应 BLE HID 规范中的 Report Reference Descriptor
 */
typedef struct
{
    uint8_t report_id;     /*!< 报告 ID */
    uint8_t report_type;   /*!< 报告类型（1=Input, 2=Output, 3=Feature）*/
}hids_report_ref_t;

/**
 * @brief HID Information 结构
 *
 * 对应 HID Information Characteristic 的值
 */
typedef struct
{
    uint16_t bcdHID;       /*!< HID 规范版本号（BCD 编码，如 0x0111 = v1.11）*/
    uint8_t bCountryCode;  /*!< 国家代码（0 = 不支持本地化）*/
    uint8_t flags;         /*!< 特性标志（远程唤醒等）*/
}hids_hid_info_t;


/**
 * @brief HID LE 环境变量（全局单例）
 *
 * 这是整个 HID Profile 的核心数据结构，维护了 Profile 的完整状态：
 * - 连接控制块（最多 HID_MAX_APPS 个并发连接）
 * - GATT 接口句柄
 * - HID 实例配置
 * - 用户注册的回调函数
 */
typedef struct {
    hidd_clcb_t                  hidd_clcb[HID_MAX_APPS];  /*!< 连接控制块数组 */
    esp_gatt_if_t                gatt_if;                   /*!< GATT 接口句柄 */
    bool                         enabled;                   /*!< Profile 是否已启用 */
    bool                         is_take;                   /*!< 是否被占用 */
    bool                         is_primery;                /*!< 是否为主服务 */
    hidd_inst_t                  hidd_inst;                 /*!< HID 实例 */
    esp_hidd_event_cb_t          hidd_cb;                   /*!< 用户注册的回调函数 */
    uint8_t                      inst_id;                   /*!< 实例 ID */
} hidd_le_env_t;

extern hidd_le_env_t hidd_le_env;     /*!< 全局 HID 环境变量 */
extern uint8_t hidProtocolMode;       /*!< 全局协议模式（Report/Boot）*/

/* =====================================================================
 *                      内部函数声明
 * ===================================================================== */

/**
 * @brief 分配连接控制块
 * @param conn_id  GATT 连接 ID
 * @param bda      远程设备蓝牙地址
 */
void hidd_clcb_alloc (uint16_t conn_id, esp_bd_addr_t bda);

/**
 * @brief 释放连接控制块
 * @param conn_id  GATT 连接 ID
 * @return true=成功释放, false=未找到对应控制块
 */
bool hidd_clcb_dealloc (uint16_t conn_id);

/**
 * @brief 创建 HID LE GATT 服务
 *
 * 先创建电池服务，电池服务创建完成后再创建 HID 服务
 * （因为 HID 服务通过 Include 引用了电池服务）
 *
 * @param gatts_if  GATT 接口句柄
 */
void hidd_le_create_service(esp_gatt_if_t gatts_if);

/**
 * @brief 设置 GATT 属性值
 * @param handle  属性句柄
 * @param val_len 值的长度
 * @param value   值的指针
 */
void hidd_set_attr_value(uint16_t handle, uint16_t val_len, const uint8_t *value);

/**
 * @brief 获取 GATT 属性值
 * @param handle  属性句柄
 * @param length  输出：值的长度
 * @param value   输出：值的指针
 */
void hidd_get_attr_value(uint16_t handle, uint16_t *length, uint8_t **value);

/**
 * @brief 注册 GATT 事件回调
 * @return ESP_OK 成功
 */
esp_err_t hidd_register_cb(void);


#endif  ///__HID_DEVICE_LE_PRF__
