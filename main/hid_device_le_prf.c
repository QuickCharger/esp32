/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/**
 * @file hid_device_le_prf.c
 * @brief BLE HID Device Profile 核心实现
 *
 * 本文件是 HID Profile 的核心，包含：
 * - HID Report Map 描述符定义（定义了鼠标、键盘、消费者控制的报告格式）
 * - 电池服务 GATT 属性表
 * - HID 服务 GATT 属性表（完整的 GATT 数据库定义）
 * - GATT 事件处理回调（esp_hidd_prf_cb_hdl）
 * - 服务创建函数
 * - 连接控制块管理
 * - 报告 ID → GATT 句柄映射表初始化
 */

#include "hidd_le_prf_int.h"
#include <string.h>
#include "esp_log.h"

/* =====================================================================
 *                    数据结构定义
 * ===================================================================== */

/**
 * @brief Characteristic Presentation Format 描述符结构
 *
 * 用于描述 Characteristic 值的表示格式（单位、指数等），
 * 在电池服务的 Battery Level Characteristic 中使用。
 */
struct prf_char_pres_fmt
{
    uint16_t unit;         /*!< 单位（UUID 格式）*/
    uint16_t description;  /*!< 描述 */
    uint8_t format;        /*!< 数据格式 */
    uint8_t exponent;      /*!< 指数 */
    uint8_t name_space;    /*!< 命名空间 */
};

/* =====================================================================
 *                HID Report Map（报告描述符）
 *
 * Report Map 是 HID 规范中最重要的数据结构，它定义了设备
 * 支持的报告格式。主机读取此描述符后即可理解设备发送的
 * 数据含义。
 *
 * 本项目定义了以下报告（按顺序）：
 *   1. 鼠标（Mouse）       — Report ID 1, 5 字节
 *   2. 键盘（Keyboard）    — Report ID 2, 8 字节输入 + 1 字节 LED 输出
 *   3. 消费者控制（CC）     — Report ID 3, 2 字节
 *   4. Vendor 自定义（可选）— Report ID 4
 * ===================================================================== */

/// HID 报告映射表（运行时填充）
static hid_report_map_t hid_rpt_map[HID_NUM_REPORTS];

/**
 * @brief HID Report Map 描述符（静态数组）
 *
 * 此字节数组遵循 USB HID Report Descriptor 规范格式。
 * 每个条目的注释标注了对应的 HID 规范含义。
 */
// Keyboard report descriptor (using format for Boot interface descriptor)
static const uint8_t hidReportMap[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  // Report Id (1)
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Buttons)
    0x19, 0x01,  //     Usage Minimum (01) - Button 1
    0x29, 0x03,  //     Usage Maximum (03) - Button 3
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x75, 0x01,  //     Report Size (1)
    0x95, 0x03,  //     Report Count (3)
    0x81, 0x02,  //     Input (Data, Variable, Absolute) - Button states
    0x75, 0x05,  //     Report Size (5)
    0x95, 0x01,  //     Report Count (1)
    0x81, 0x01,  //     Input (Constant) - Padding or Reserved bits
    0x05, 0x01,  //     Usage Page (Generic Desktop)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x09, 0x38,  //     Usage (Wheel)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x03,  //     Report Count (3)
    0x81, 0x06,  //     Input (Data, Variable, Relative) - X & Y coordinate
    0xC0,        //   End Collection
    0xC0,        // End Collection

    0x05, 0x01,  // Usage Pg (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection: (Application)
    0x85, 0x02,  // Report Id (2)
    //
    0x05, 0x07,  //   Usage Pg (Key Codes)
    0x19, 0xE0,  //   Usage Min (224)
    0x29, 0xE7,  //   Usage Max (231)
    0x15, 0x00,  //   Log Min (0)
    0x25, 0x01,  //   Log Max (1)
    //
    //   Modifier byte
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input: (Data, Variable, Absolute)
    //
    //   Reserved byte
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input: (Constant)
    //
    //   LED report
    0x05, 0x08,  //   Usage Pg (LEDs)
    0x19, 0x01,  //   Usage Min (1)
    0x29, 0x05,  //   Usage Max (5)
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x91, 0x02,  //   Output: (Data, Variable, Absolute)
    //
    //   LED report padding
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output: (Constant)
    //
    //   Key arrays (6 bytes)
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Log Min (0)
    0x25, 0x65,  //   Log Max (101)
    0x05, 0x07,  //   Usage Pg (Key Codes)
    0x19, 0x00,  //   Usage Min (0)
    0x29, 0x65,  //   Usage Max (101)
    0x81, 0x00,  //   Input: (Data, Array)
    //
    0xC0,        // End Collection
    //
    0x05, 0x0C,   // Usage Pg (Consumer Devices)
    0x09, 0x01,   // Usage (Consumer Control)
    0xA1, 0x01,   // Collection (Application)
    0x85, 0x03,   // Report Id (3)
    0x09, 0x02,   //   Usage (Numeric Key Pad)
    0xA1, 0x02,   //   Collection (Logical)
    0x05, 0x09,   //     Usage Pg (Button)
    0x19, 0x01,   //     Usage Min (Button 1)
    0x29, 0x0A,   //     Usage Max (Button 10)
    0x15, 0x01,   //     Logical Min (1)
    0x25, 0x0A,   //     Logical Max (10)
    0x75, 0x04,   //     Report Size (4)
    0x95, 0x01,   //     Report Count (1)
    0x81, 0x00,   //     Input (Data, Ary, Abs)
    0xC0,         //   End Collection
    0x05, 0x0C,   //   Usage Pg (Consumer Devices)
    0x09, 0x86,   //   Usage (Channel)
    0x15, 0xFF,   //   Logical Min (-1)
    0x25, 0x01,   //   Logical Max (1)
    0x75, 0x02,   //   Report Size (2)
    0x95, 0x01,   //   Report Count (1)
    0x81, 0x46,   //   Input (Data, Var, Rel, Null)
    0x09, 0xE9,   //   Usage (Volume Up)
    0x09, 0xEA,   //   Usage (Volume Down)
    0x15, 0x00,   //   Logical Min (0)
    0x75, 0x01,   //   Report Size (1)
    0x95, 0x02,   //   Report Count (2)
    0x81, 0x02,   //   Input (Data, Var, Abs)
    0x09, 0xE2,   //   Usage (Mute)
    0x09, 0x30,   //   Usage (Power)
    0x09, 0x83,   //   Usage (Recall Last)
    0x09, 0x81,   //   Usage (Assign Selection)
    0x09, 0xB0,   //   Usage (Play)
    0x09, 0xB1,   //   Usage (Pause)
    0x09, 0xB2,   //   Usage (Record)
    0x09, 0xB3,   //   Usage (Fast Forward)
    0x09, 0xB4,   //   Usage (Rewind)
    0x09, 0xB5,   //   Usage (Scan Next)
    0x09, 0xB6,   //   Usage (Scan Prev)
    0x09, 0xB7,   //   Usage (Stop)
    0x15, 0x01,   //   Logical Min (1)
    0x25, 0x0C,   //   Logical Max (12)
    0x75, 0x04,   //   Report Size (4)
    0x95, 0x01,   //   Report Count (1)
    0x81, 0x00,   //   Input (Data, Ary, Abs)
    0x09, 0x80,   //   Usage (Selection)
    0xA1, 0x02,   //   Collection (Logical)
    0x05, 0x09,   //     Usage Pg (Button)
    0x19, 0x01,   //     Usage Min (Button 1)
    0x29, 0x03,   //     Usage Max (Button 3)
    0x15, 0x01,   //     Logical Min (1)
    0x25, 0x03,   //     Logical Max (3)
    0x75, 0x02,   //     Report Size (2)
    0x81, 0x00,   //     Input (Data, Ary, Abs)
    0xC0,           //   End Collection
    0x81, 0x03,   //   Input (Const, Var, Abs)
    0xC0,            // End Collectionq

#if (SUPPORT_REPORT_VENDOR == true)
    0x06, 0xFF, 0xFF, // Usage Page(Vendor defined)
    0x09, 0xA5,       // Usage(Vendor Defined)
    0xA1, 0x01,       // Collection(Application)
    0x85, 0x04,   // Report Id (4)
    0x09, 0xA6,   // Usage(Vendor defined)
    0x09, 0xA9,   // Usage(Vendor defined)
    0x75, 0x08,   // Report Size
    0x95, 0x7F,   // Report Count = 127 Btyes
    0x91, 0x02,   // Output(Data, Variable, Absolute)
    0xC0,         // End Collection
#endif

};  // hidReportMap 结束

/* =====================================================================
 *                    电池服务（Battery Service）
 *
 * 电池服务是 HID 规范要求的附加服务，HID 服务通过 Include
 * 引用它。此处定义了一个简单的电池服务，固定报告电量 50%。
 * ===================================================================== */

/// 电池服务 GATT 属性表索引
enum
{
    BAS_IDX_SVC,

    BAS_IDX_BATT_LVL_CHAR,
    BAS_IDX_BATT_LVL_VAL,
    BAS_IDX_BATT_LVL_NTF_CFG,
    BAS_IDX_BATT_LVL_PRES_FMT,

    BAS_IDX_NB,
};

#define HI_UINT16(a) (((a) >> 8) & 0xFF)          /*!< 获取 16 位值的高字节 */
#define LO_UINT16(a) ((a) & 0xFF)                  /*!< 获取 16 位值的低字节 */
#define PROFILE_NUM            1                    /*!< GATT Profile 数量 */
#define PROFILE_APP_IDX        0                    /*!< Profile 数组索引 */

/**
 * @brief GATT Profile 实例结构体
 *
 * 每个 Profile 实例包含一个 GATT 回调函数和对应的 GATT 接口句柄。
 */
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;       /*!< GATT 事件回调函数 */
    uint16_t gatts_if;             /*!< GATT 接口句柄 */
    uint16_t app_id;               /*!< 应用 ID */
    uint16_t conn_id;              /*!< 连接 ID */
};

/* =====================================================================
 *                    全局变量定义
 * ===================================================================== */

hidd_le_env_t hidd_le_env;                           /*!< HID Profile 全局环境变量（单例）*/

uint8_t hidReportMapLen = sizeof(hidReportMap);      /*!< Report Map 描述符长度 */
uint8_t hidProtocolMode = HID_PROTOCOL_MODE_REPORT;  /*!< 当前协议模式（默认 Report Mode）*/

/* =====================================================================
 *                HID Characteristic 静态值定义
 * ===================================================================== */

/**
 * @brief HID Information Characteristic 值
 *
 * 格式：bcdHID(2字节) + bCountryCode(1字节) + Flags(1字节)
 * - bcdHID = 0x0111：HID 规范版本 1.11
 * - bCountryCode = 0：不支持本地化
 * - Flags = HID_KBD_FLAGS：支持远程唤醒
 */
static const uint8_t hidInfo[HID_INFORMATION_LEN] = {
    LO_UINT16(0x0111), HI_UINT16(0x0111),             /* bcdHID (USB HID 版本 1.11) */
    0x00,                                             /* bCountryCode (不支持本地化) */
    HID_KBD_FLAGS                                     /* Flags (远程唤醒) */
};

/// HID External Report Reference Descriptor — 指向电池服务的 Battery Level
static uint16_t hidExtReportRefDesc = ESP_GATT_UUID_BATTERY_LEVEL;

/**
 * @brief 各报告的 Report Reference Descriptor 值
 *
 * 每个描述符由 2 字节组成：[Report ID, Report Type]
 * Report Type: 1=Input, 2=Output, 3=Feature
 */

static uint8_t hidReportRefMouseIn[HID_REPORT_REF_LEN] =       /* 鼠标输入 */
             { HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT };

static uint8_t hidReportRefKeyIn[HID_REPORT_REF_LEN] =         /* 键盘输入 */
             { HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT };

static uint8_t hidReportRefLedOut[HID_REPORT_REF_LEN] =        /* LED 输出 */
             { HID_RPT_ID_LED_OUT, HID_REPORT_TYPE_OUTPUT };

#if (SUPPORT_REPORT_VENDOR  == true)
static uint8_t hidReportRefVendorOut[HID_REPORT_REF_LEN] =     /* Vendor 输出（默认关闭）*/
             {HID_RPT_ID_VENDOR_OUT, HID_REPORT_TYPE_OUTPUT};
#endif

static uint8_t hidReportRefFeature[HID_REPORT_REF_LEN] =       /* Feature 报告 */
             { HID_RPT_ID_FEATURE, HID_REPORT_TYPE_FEATURE };

static uint8_t hidReportRefCCIn[HID_REPORT_REF_LEN] =          /* 消费者控制输入 */
             { HID_RPT_ID_CC_IN, HID_REPORT_TYPE_INPUT };

/* =====================================================================
 *                GATT 属性表使用的 UUID 和属性定义
 * ===================================================================== */

/// HID 服务 UUID（16-bit：0x1812）
static uint16_t hid_le_svc = ATT_SVC_HID;
uint16_t            hid_count = 0;
esp_gatts_incl_svc_desc_t incl_svc = {0};            /*!< Include Service 描述符（运行时填充）*/

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))     /*!< Characteristic 声明属性大小 */
///the uuid definition
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t include_service_uuid = ESP_GATT_UUID_INCLUDE_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t hid_info_char_uuid = ESP_GATT_UUID_HID_INFORMATION;
static const uint16_t hid_report_map_uuid    = ESP_GATT_UUID_HID_REPORT_MAP;
static const uint16_t hid_control_point_uuid = ESP_GATT_UUID_HID_CONTROL_POINT;
static const uint16_t hid_report_uuid = ESP_GATT_UUID_HID_REPORT;
static const uint16_t hid_proto_mode_uuid = ESP_GATT_UUID_HID_PROTO_MODE;
static const uint16_t hid_kb_input_uuid = ESP_GATT_UUID_HID_BT_KB_INPUT;
static const uint16_t hid_kb_output_uuid = ESP_GATT_UUID_HID_BT_KB_OUTPUT;
static const uint16_t hid_mouse_input_uuid = ESP_GATT_UUID_HID_BT_MOUSE_INPUT;
static const uint16_t hid_repot_map_ext_desc_uuid = ESP_GATT_UUID_EXT_RPT_REF_DESCR;
static const uint16_t hid_report_ref_descr_uuid = ESP_GATT_UUID_RPT_REF_DESCR;
///the propoty definition
static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_write_nr = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE|ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read_write_notify = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE|ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read_write_write_nr = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE|ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

/// battary Service
static const uint16_t battary_svc = ESP_GATT_UUID_BATTERY_SERVICE_SVC;

static const uint16_t bat_lev_uuid = ESP_GATT_UUID_BATTERY_LEVEL;
static const uint8_t   bat_lev_ccc[2] ={ 0x00, 0x00};
static const uint16_t char_format_uuid = ESP_GATT_UUID_CHAR_PRESENT_FORMAT;

static uint8_t battary_lev = 50;
/// Full HRS Database Description - Used to add attributes into the database
static const esp_gatts_attr_db_t bas_att_db[BAS_IDX_NB] =
{
    // Battary Service Declaration
    [BAS_IDX_SVC]               =  {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
                                            sizeof(uint16_t), sizeof(battary_svc), (uint8_t *)&battary_svc}},

    // Battary level Characteristic Declaration
    [BAS_IDX_BATT_LVL_CHAR]    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
                                                   CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    // Battary level Characteristic Value
    [BAS_IDX_BATT_LVL_VAL]             	= {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&bat_lev_uuid, ESP_GATT_PERM_READ,
                                                                sizeof(uint8_t),sizeof(uint8_t), &battary_lev}},

    // Battary level Characteristic - Client Characteristic Configuration Descriptor
    [BAS_IDX_BATT_LVL_NTF_CFG]     	=  {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
                                                          sizeof(uint16_t),sizeof(bat_lev_ccc), (uint8_t *)bat_lev_ccc}},

    // Battary level report Characteristic Declaration
    [BAS_IDX_BATT_LVL_PRES_FMT]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_format_uuid, ESP_GATT_PERM_READ,
                                                        sizeof(struct prf_char_pres_fmt), 0, NULL}},
};


/// Full Hid device Database Description - Used to add attributes into the database
static esp_gatts_attr_db_t hidd_le_gatt_db[HIDD_LE_IDX_NB] =
{
            // HID Service Declaration
    [HIDD_LE_IDX_SVC]                       = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid,
                                                             ESP_GATT_PERM_READ_ENCRYPTED, sizeof(uint16_t), sizeof(hid_le_svc),
                                                            (uint8_t *)&hid_le_svc}},

    // HID Service Declaration
    [HIDD_LE_IDX_INCL_SVC]               = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&include_service_uuid,
                                                            ESP_GATT_PERM_READ,
                                                            sizeof(esp_gatts_incl_svc_desc_t), sizeof(esp_gatts_incl_svc_desc_t),
                                                            (uint8_t *)&incl_svc}},

    // HID Information Characteristic Declaration
    [HIDD_LE_IDX_HID_INFO_CHAR]     = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                            ESP_GATT_PERM_READ,
                                                            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                            (uint8_t *)&char_prop_read}},
    // HID Information Characteristic Value
    [HIDD_LE_IDX_HID_INFO_VAL]       = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_info_char_uuid,
                                                            ESP_GATT_PERM_READ,
                                                            sizeof(hids_hid_info_t), sizeof(hidInfo),
                                                            (uint8_t *)&hidInfo}},

    // HID Control Point Characteristic Declaration
    [HIDD_LE_IDX_HID_CTNL_PT_CHAR]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                              ESP_GATT_PERM_READ,
                                                              CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                              (uint8_t *)&char_prop_write_nr}},
    // HID Control Point Characteristic Value
    [HIDD_LE_IDX_HID_CTNL_PT_VAL]    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_control_point_uuid,
                                                             ESP_GATT_PERM_WRITE,
                                                             sizeof(uint8_t), 0,
                                                             NULL}},

    // Report Map Characteristic Declaration
    [HIDD_LE_IDX_REPORT_MAP_CHAR]   = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                              ESP_GATT_PERM_READ,
                                                              CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                              (uint8_t *)&char_prop_read}},
    // Report Map Characteristic Value
    [HIDD_LE_IDX_REPORT_MAP_VAL]     = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_map_uuid,
                                                              ESP_GATT_PERM_READ,
                                                              HIDD_LE_REPORT_MAP_MAX_LEN, sizeof(hidReportMap),
                                                              (uint8_t *)&hidReportMap}},

    // Report Map Characteristic - External Report Reference Descriptor
    [HIDD_LE_IDX_REPORT_MAP_EXT_REP_REF]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_repot_map_ext_desc_uuid,
                                                                        ESP_GATT_PERM_READ,
                                                                        sizeof(uint16_t), sizeof(uint16_t),
                                                                        (uint8_t *)&hidExtReportRefDesc}},

    // Protocol Mode Characteristic Declaration
    [HIDD_LE_IDX_PROTO_MODE_CHAR]            = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                        ESP_GATT_PERM_READ,
                                                                        CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                        (uint8_t *)&char_prop_read_write}},
    // Protocol Mode Characteristic Value
    [HIDD_LE_IDX_PROTO_MODE_VAL]               = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_proto_mode_uuid,
                                                                        (ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE),
                                                                        sizeof(uint8_t), sizeof(hidProtocolMode),
                                                                        (uint8_t *)&hidProtocolMode}},

    [HIDD_LE_IDX_REPORT_MOUSE_IN_CHAR]       = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                         ESP_GATT_PERM_READ,
                                                                         CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                         (uint8_t *)&char_prop_read_notify}},

    [HIDD_LE_IDX_REPORT_MOUSE_IN_VAL]        = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       HIDD_LE_REPORT_MAX_LEN, 0,
                                                                       NULL}},

    [HIDD_LE_IDX_REPORT_MOUSE_IN_CCC]        = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
                                                                      (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE),
                                                                      sizeof(uint16_t), 0,
                                                                      NULL}},

    [HIDD_LE_IDX_REPORT_MOUSE_REP_REF]       = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       sizeof(hidReportRefMouseIn), sizeof(hidReportRefMouseIn),
                                                                       hidReportRefMouseIn}},
    // Report Characteristic Declaration
    [HIDD_LE_IDX_REPORT_KEY_IN_CHAR]         = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                         ESP_GATT_PERM_READ,
                                                                         CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                         (uint8_t *)&char_prop_read_notify}},
    // Report Characteristic Value
    [HIDD_LE_IDX_REPORT_KEY_IN_VAL]            = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       HIDD_LE_REPORT_MAX_LEN, 0,
                                                                       NULL}},
    // Report KEY INPUT Characteristic - Client Characteristic Configuration Descriptor
    [HIDD_LE_IDX_REPORT_KEY_IN_CCC]              = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
                                                                      (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE),
                                                                      sizeof(uint16_t), 0,
                                                                      NULL}},
     // Report Characteristic - Report Reference Descriptor
    [HIDD_LE_IDX_REPORT_KEY_IN_REP_REF]       = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       sizeof(hidReportRefKeyIn), sizeof(hidReportRefKeyIn),
                                                                       hidReportRefKeyIn}},

     // Report Characteristic Declaration
    [HIDD_LE_IDX_REPORT_LED_OUT_CHAR]         = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                         ESP_GATT_PERM_READ,
                                                                         CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                         (uint8_t *)&char_prop_read_write_write_nr}},

    [HIDD_LE_IDX_REPORT_LED_OUT_VAL]            = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid,
                                                                       ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
                                                                       HIDD_LE_REPORT_MAX_LEN, 0,
                                                                       NULL}},
    [HIDD_LE_IDX_REPORT_LED_OUT_REP_REF]      =  {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       sizeof(hidReportRefLedOut), sizeof(hidReportRefLedOut),
                                                                       hidReportRefLedOut}},
#if (SUPPORT_REPORT_VENDOR  == true)
    // Report Characteristic Declaration
    [HIDD_LE_IDX_REPORT_VENDOR_OUT_CHAR]        = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                         ESP_GATT_PERM_READ,
                                                                         CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                         (uint8_t *)&char_prop_read_write_notify}},
    [HIDD_LE_IDX_REPORT_VENDOR_OUT_VAL]         = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid,
                                                                       ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
                                                                       HIDD_LE_REPORT_MAX_LEN, 0,
                                                                       NULL}},
    [HIDD_LE_IDX_REPORT_VENDOR_OUT_REP_REF]     = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       sizeof(hidReportRefVendorOut), sizeof(hidReportRefVendorOut),
                                                                       hidReportRefVendorOut}},
#endif
    // Report Characteristic Declaration
    [HIDD_LE_IDX_REPORT_CC_IN_CHAR]         = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                         ESP_GATT_PERM_READ,
                                                                         CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                         (uint8_t *)&char_prop_read_notify}},
    // Report Characteristic Value
    [HIDD_LE_IDX_REPORT_CC_IN_VAL]            = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       HIDD_LE_REPORT_MAX_LEN, 0,
                                                                       NULL}},
    // Report KEY INPUT Characteristic - Client Characteristic Configuration Descriptor
    [HIDD_LE_IDX_REPORT_CC_IN_CCC]              = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
                                                                      (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED),
                                                                      sizeof(uint16_t), 0,
                                                                      NULL}},
     // Report Characteristic - Report Reference Descriptor
    [HIDD_LE_IDX_REPORT_CC_IN_REP_REF]       = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       sizeof(hidReportRefCCIn), sizeof(hidReportRefCCIn),
                                                                       hidReportRefCCIn}},

    // Boot Keyboard Input Report Characteristic Declaration
    [HIDD_LE_IDX_BOOT_KB_IN_REPORT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                        ESP_GATT_PERM_READ,
                                                                        CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                        (uint8_t *)&char_prop_read_notify}},
    // Boot Keyboard Input Report Characteristic Value
    [HIDD_LE_IDX_BOOT_KB_IN_REPORT_VAL]   = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_kb_input_uuid,
                                                                        ESP_GATT_PERM_READ,
                                                                        HIDD_LE_BOOT_REPORT_MAX_LEN, 0,
                                                                        NULL}},
    // Boot Keyboard Input Report Characteristic - Client Characteristic Configuration Descriptor
    [HIDD_LE_IDX_BOOT_KB_IN_REPORT_NTF_CFG]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
                                                                              (ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE),
                                                                              sizeof(uint16_t), 0,
                                                                              NULL}},

    // Boot Keyboard Output Report Characteristic Declaration
    [HIDD_LE_IDX_BOOT_KB_OUT_REPORT_CHAR]    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                              ESP_GATT_PERM_READ,
                                                                              CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                              (uint8_t *)&char_prop_read_write}},
    // Boot Keyboard Output Report Characteristic Value
    [HIDD_LE_IDX_BOOT_KB_OUT_REPORT_VAL]      = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_kb_output_uuid,
                                                                              (ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE),
                                                                              HIDD_LE_BOOT_REPORT_MAX_LEN, 0,
                                                                              NULL}},

    // Boot Mouse Input Report Characteristic Declaration
    [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                              ESP_GATT_PERM_READ,
                                                                              CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                              (uint8_t *)&char_prop_read_notify}},
    // Boot Mouse Input Report Characteristic Value
    [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_VAL]   = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_mouse_input_uuid,
                                                                              ESP_GATT_PERM_READ,
                                                                              HIDD_LE_BOOT_REPORT_MAX_LEN, 0,
                                                                              NULL}},
    // Boot Mouse Input Report Characteristic - Client Characteristic Configuration Descriptor
    [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_NTF_CFG]    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
                                                                                      (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE),
                                                                                      sizeof(uint16_t), 0,
                                                                                      NULL}},

    // Report Characteristic Declaration
    [HIDD_LE_IDX_REPORT_CHAR]                    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                                                                         ESP_GATT_PERM_READ,
                                                                         CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
                                                                         (uint8_t *)&char_prop_read_write}},
    // Report Characteristic Value
    [HIDD_LE_IDX_REPORT_VAL]                      = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       HIDD_LE_REPORT_MAX_LEN, 0,
                                                                       NULL}},
    // Report Characteristic - Report Reference Descriptor
    [HIDD_LE_IDX_REPORT_REP_REF]               = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid,
                                                                       ESP_GATT_PERM_READ,
                                                                       sizeof(hidReportRefFeature), sizeof(hidReportRefFeature),
                                                                       hidReportRefFeature}},
};  // hidd_le_gatt_db 属性表结束

/* =====================================================================
 *                    核心函数实现
 * ===================================================================== */

static void hid_add_id_tbl(void);

/**
 * @brief HID Device Profile GATT 事件处理回调（核心）
 *
 * 这是整个 HID Profile 最核心的回调函数，处理所有 GATT 事件：
 *
 * ESP_GATTS_REG_EVT:
 *   应用注册完成。区分 HIDD_APP_ID 和 BATTRAY_APP_ID 两个应用。
 *   - HIDD_APP_ID: 保存 gatt_if，通知上层注册完成，开始创建服务
 *   - BATTRAY_APP_ID: 仅通知上层电池服务注册完成
 *
 * ESP_GATTS_CONNECT_EVT:
 *   连接建立。分配连接控制块（CLCB），请求加密。
 *
 * ESP_GATTS_DISCONNECT_EVT:
 *   连接断开。通知上层，释放连接控制块。
 *
 * ESP_GATTS_WRITE_EVT:
 *   属性写入。判断是 LED 输出报告还是 Vendor 报告写入，
 *   分别通知上层。
 *
 * ESP_GATTS_CREAT_ATTR_TAB_EVT:
 *   属性表创建完成。关键逻辑：
 *   - 电池服务创建成功后，记录其句柄范围（用于 Include），然后创建 HID 服务
 *   - HID 服务创建成功后，保存句柄表，建立报告映射表，启动服务
 */
void esp_hidd_prf_cb_hdl(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
									esp_ble_gatts_cb_param_t *param)
{
    switch(event) {
        case ESP_GATTS_REG_EVT: {                           /* GATT 应用注册事件 */
            esp_ble_gap_config_local_icon (ESP_BLE_APPEARANCE_GENERIC_HID);  // 设置外观为 HID 设备
            esp_hidd_cb_param_t hidd_param;
            hidd_param.init_finish.state = param->reg.status;
            if(param->reg.app_id == HIDD_APP_ID) {           // HID 服务应用注册完成
                hidd_le_env.gatt_if = gatts_if;              // 保存 GATT 接口句柄
                if(hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_REG_FINISH, &hidd_param);
                    hidd_le_create_service(hidd_le_env.gatt_if);  // 开始创建 GATT 服务
                }
            }
            if(param->reg.app_id == BATTRAY_APP_ID) {        // 电池服务应用注册完成
                hidd_param.init_finish.gatts_if = gatts_if;
                 if(hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_BAT_EVENT_REG, &hidd_param);
                }

            }

            break;
        }
        case ESP_GATTS_CONF_EVT: {                           /* GATT 确认事件（Indication 被确认）*/
            break;
        }
        case ESP_GATTS_CREATE_EVT:                           /* GATT 创建事件 */
            break;
        case ESP_GATTS_CONNECT_EVT: {                        /* BLE 连接建立 */
            esp_hidd_cb_param_t cb_param = {0};
			ESP_LOGI(HID_LE_PRF_TAG, "HID connection establish, conn_id = %x",param->connect.conn_id);
			memcpy(cb_param.connect.remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            cb_param.connect.conn_id = param->connect.conn_id;
            hidd_clcb_alloc(param->connect.conn_id, param->connect.remote_bda);  // 分配连接控制块
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);  // 请求加密
            if(hidd_le_env.hidd_cb != NULL) {
                (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_CONNECT, &cb_param);
            }
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT: {                     /* BLE 连接断开 */
			 if(hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_DISCONNECT, NULL);
             }
            hidd_clcb_dealloc(param->disconnect.conn_id);     // 释放连接控制块
            break;
        }
        case ESP_GATTS_CLOSE_EVT:                            /* GATT 关闭事件 */
            break;
        case ESP_GATTS_WRITE_EVT: {                          /* GATT 写事件 */
            esp_hidd_cb_param_t cb_param = {0};
            // 判断是否是 LED 输出报告写入（主机控制键盘指示灯）
            if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_LED_OUT_VAL]) {
                cb_param.led_write.conn_id = param->write.conn_id;
                cb_param.led_write.report_id = HID_RPT_ID_LED_OUT;
                cb_param.led_write.length = param->write.len;
                cb_param.led_write.data = param->write.value;
                (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT, &cb_param);
            }
#if (SUPPORT_REPORT_VENDOR == true)
            // 判断是否是 Vendor 报告写入（默认关闭）
            if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_VENDOR_OUT_VAL] &&
                hidd_le_env.hidd_cb != NULL) {
                cb_param.vendor_write.conn_id = param->write.conn_id;
                cb_param.vendor_write.report_id = HID_RPT_ID_VENDOR_OUT;
                cb_param.vendor_write.length = param->write.len;
                cb_param.vendor_write.data = param->write.value;
                (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT, &cb_param);
            }
#endif
            break;
        }
        case ESP_GATTS_CREAT_ATTR_TAB_EVT: {                 /* GATT 属性表创建完成 */
            // 电池服务属性表创建完成 → 记录句柄范围，然后创建 HID 服务
            if (param->add_attr_tab.num_handle == BAS_IDX_NB &&
                param->add_attr_tab.svc_uuid.uuid.uuid16 == ESP_GATT_UUID_BATTERY_SERVICE_SVC &&
                param->add_attr_tab.status == ESP_GATT_OK) {
                incl_svc.start_hdl = param->add_attr_tab.handles[BAS_IDX_SVC];  // 记录电池服务起始句柄
                incl_svc.end_hdl = incl_svc.start_hdl + BAS_IDX_NB -1;          // 计算电池服务结束句柄
                ESP_LOGI(HID_LE_PRF_TAG, "%s(), start added the hid service to the stack database. incl_handle = %d",
                           __func__, incl_svc.start_hdl);
                esp_ble_gatts_create_attr_tab(hidd_le_gatt_db, gatts_if, HIDD_LE_IDX_NB, 0);  // 创建 HID 服务属性表
            }
            // HID 服务属性表创建完成 → 保存句柄表，建立报告映射，启动服务
            if (param->add_attr_tab.num_handle == HIDD_LE_IDX_NB &&
                param->add_attr_tab.status == ESP_GATT_OK) {
                memcpy(hidd_le_env.hidd_inst.att_tbl, param->add_attr_tab.handles,
                            HIDD_LE_IDX_NB*sizeof(uint16_t));  // 保存所有属性句柄
                ESP_LOGI(HID_LE_PRF_TAG, "hid svc handle = %x",hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC]);
                hid_add_id_tbl();                              // 建立报告 ID → GATT 句柄映射表
		        esp_ble_gatts_start_service(hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC]);  // 启动 HID 服务
            } else {
                esp_ble_gatts_start_service(param->add_attr_tab.handles[0]);  // 启动其他服务（电池服务）
            }
            break;
         }

        default:
            break;
    }
}

/**
 * @brief 创建 HID LE GATT 服务
 *
 * 先创建电池服务（BAS），电池服务创建成功后的回调中再创建 HID 服务。
 * 这个顺序是必需的，因为 HID 服务通过 Include 引用了电池服务。
 */
void hidd_le_create_service(esp_gatt_if_t gatts_if)
{
    /* 必须先创建电池服务，因为 HID 服务需要 Include 电池服务。
       电池服务创建完成后，在 ESP_GATTS_CREAT_ATTR_TAB_EVT 回调中再创建 HID 服务。 */
    esp_ble_gatts_create_attr_tab(bas_att_db, gatts_if, BAS_IDX_NB, 0);

}

/**
 * @brief 初始化 HID LE 环境
 */
void hidd_le_init(void)
{
    // 重置 HID 设备环境变量
    memset(&hidd_le_env, 0, sizeof(hidd_le_env_t));
}

/**
 * @brief 分配连接控制块（CLCB）
 *
 * 在连接建立时，从 hidd_clcb 数组中找到一个空闲的条目，
 * 记录连接 ID 和远程设备地址。
 */
void hidd_clcb_alloc (uint16_t conn_id, esp_bd_addr_t bda)
{
    uint8_t                   i_clcb = 0;
    hidd_clcb_t      *p_clcb = NULL;

    for (i_clcb = 0, p_clcb= hidd_le_env.hidd_clcb; i_clcb < HID_MAX_APPS; i_clcb++, p_clcb++) {
        if (!p_clcb->in_use) {                       // 找到空闲的控制块
            p_clcb->in_use      = true;
            p_clcb->conn_id     = conn_id;
            p_clcb->connected   = true;
            memcpy (p_clcb->remote_bda, bda, ESP_BD_ADDR_LEN);
            break;
        }
    }
    return;
}

/**
 * @brief 释放连接控制块（CLCB）
 *
 * 连接断开时，清空对应的控制块条目。
 *
 * @note 当前实现简单地清空第一个条目，适用于单连接场景。
 *       多连接场景下应根据 conn_id 查找对应的条目。
 */
bool hidd_clcb_dealloc (uint16_t conn_id)
{
    uint8_t              i_clcb = 0;
    hidd_clcb_t      *p_clcb = NULL;

    for (i_clcb = 0, p_clcb= hidd_le_env.hidd_clcb; i_clcb < HID_MAX_APPS; i_clcb++, p_clcb++) {
            memset(p_clcb, 0, sizeof(hidd_clcb_t));  // 清空控制块
            return true;
    }

    return false;
}

/** @brief GATT Profile 实例表 */
static struct gatts_profile_inst heart_rate_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = esp_hidd_prf_cb_hdl,             /* GATT 回调函数 */
        .gatts_if = ESP_GATT_IF_NONE,                /* 初始值：尚未获取 gatt_if */
    },

};

/**
 * @brief GATT 事件分发处理函数
 *
 * ESP-IDF GATT 层的事件会先到达这里，然后根据 gatts_if 分发给
 * 对应 Profile 的回调函数（esp_hidd_prf_cb_hdl）。
 *
 * 对于 ESP_GATTS_REG_EVT 事件，先保存 gatts_if。
 */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    /* 注册事件：保存 gatts_if 到 profile 实例 */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            heart_rate_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGI(HID_LE_PRF_TAG, "Reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* 遍历所有 Profile 实例，将事件分发给匹配的回调函数 */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE ||          /* 不指定 gatts_if，需要调用所有 profile 回调 */
                    gatts_if == heart_rate_profile_tab[idx].gatts_if) {
                if (heart_rate_profile_tab[idx].gatts_cb) {
                    heart_rate_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}


/**
 * @brief 注册 GATT 事件回调
 *
 * 向 ESP-IDF GATT 层注册 gatts_event_handler 作为事件入口。
 */
esp_err_t hidd_register_cb(void)
{
	esp_err_t status;
	status = esp_ble_gatts_register_callback(gatts_event_handler);
	return status;
}

/**
 * @brief 设置 GATT 属性值（带句柄范围检查）
 */
void hidd_set_attr_value(uint16_t handle, uint16_t val_len, const uint8_t *value)
{
    hidd_inst_t *hidd_inst = &hidd_le_env.hidd_inst;
    if(hidd_inst->att_tbl[HIDD_LE_IDX_HID_INFO_VAL] <= handle &&
        hidd_inst->att_tbl[HIDD_LE_IDX_REPORT_REP_REF] >= handle) {
        esp_ble_gatts_set_attr_value(handle, val_len, value);
    } else {
        ESP_LOGE(HID_LE_PRF_TAG, "%s error:Invalid handle value.",__func__);
    }
    return;
}

/**
 * @brief 获取 GATT 属性值（带句柄范围检查）
 */
void hidd_get_attr_value(uint16_t handle, uint16_t *length, uint8_t **value)
{
    hidd_inst_t *hidd_inst = &hidd_le_env.hidd_inst;
    if(hidd_inst->att_tbl[HIDD_LE_IDX_HID_INFO_VAL] <= handle &&
        hidd_inst->att_tbl[HIDD_LE_IDX_REPORT_REP_REF] >= handle){
        esp_ble_gatts_get_attr_value(handle, length, (const uint8_t **)value);
    } else {
        ESP_LOGE(HID_LE_PRF_TAG, "%s error:Invalid handle value.", __func__);
    }

    return;
}

/**
 * @brief 建立报告 ID → GATT 句柄映射表
 *
 * 在 HID 服务属性表创建完成后调用，将每个报告的 ID、类型
 * 和协议模式映射到对应的 GATT Characteristic 句柄。
 *
 * 映射表共 9 个条目（HID_NUM_REPORTS）：
 *   [0] 鼠标输入（Report Mode）         [4] Boot 键盘输入
 *   [1] 键盘输入（Report Mode）         [5] Boot 键盘输出
 *   [2] Consumer Control 输入           [6] Boot 鼠标输入
 *   [3] LED 输出                        [7] Feature 报告
 *                                       [8] （保留）
 *
 * 后续发送报告时，通过 hid_dev_rpt_by_id() 根据报告 ID 和类型
 * 查找此表，获取对应的 GATT 句柄。
 */
static void hid_add_id_tbl(void)
{
     // [0] 鼠标输入报告（Report Mode）
      hid_rpt_map[0].id = hidReportRefMouseIn[0];
      hid_rpt_map[0].type = hidReportRefMouseIn[1];
      hid_rpt_map[0].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_VAL];
      hid_rpt_map[0].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_VAL];
      hid_rpt_map[0].mode = HID_PROTOCOL_MODE_REPORT;

      // [1] 键盘输入报告（Report Mode）
      hid_rpt_map[1].id = hidReportRefKeyIn[0];
      hid_rpt_map[1].type = hidReportRefKeyIn[1];
      hid_rpt_map[1].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_KEY_IN_VAL];
      hid_rpt_map[1].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_KEY_IN_CCC];
      hid_rpt_map[1].mode = HID_PROTOCOL_MODE_REPORT;

      // [2] Consumer Control 输入报告（Report Mode）
      hid_rpt_map[2].id = hidReportRefCCIn[0];
      hid_rpt_map[2].type = hidReportRefCCIn[1];
      hid_rpt_map[2].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_CC_IN_VAL];
      hid_rpt_map[2].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_CC_IN_CCC];
      hid_rpt_map[2].mode = HID_PROTOCOL_MODE_REPORT;

      // [3] LED 输出报告（Report Mode）
      hid_rpt_map[3].id = hidReportRefLedOut[0];
      hid_rpt_map[3].type = hidReportRefLedOut[1];
      hid_rpt_map[3].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_LED_OUT_VAL];
      hid_rpt_map[3].cccdHandle = 0;
      hid_rpt_map[3].mode = HID_PROTOCOL_MODE_REPORT;

      // [4] Boot 键盘输入报告（Boot Mode，兼容 BIOS 等简单系统）
      hid_rpt_map[4].id = hidReportRefKeyIn[0];
      hid_rpt_map[4].type = hidReportRefKeyIn[1];
      hid_rpt_map[4].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_BOOT_KB_IN_REPORT_VAL];
      hid_rpt_map[4].cccdHandle = 0;
      hid_rpt_map[4].mode = HID_PROTOCOL_MODE_BOOT;

      // [5] Boot 键盘输出报告（Boot Mode）
      hid_rpt_map[5].id = hidReportRefLedOut[0];
      hid_rpt_map[5].type = hidReportRefLedOut[1];
      hid_rpt_map[5].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_BOOT_KB_OUT_REPORT_VAL];
      hid_rpt_map[5].cccdHandle = 0;
      hid_rpt_map[5].mode = HID_PROTOCOL_MODE_BOOT;

      // [6] Boot 鼠标输入报告（Boot Mode）
      hid_rpt_map[6].id = hidReportRefMouseIn[0];
      hid_rpt_map[6].type = hidReportRefMouseIn[1];
      hid_rpt_map[6].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_VAL];
      hid_rpt_map[6].cccdHandle = 0;
      hid_rpt_map[6].mode = HID_PROTOCOL_MODE_BOOT;

      // [7] Feature 报告
      hid_rpt_map[7].id = hidReportRefFeature[0];
      hid_rpt_map[7].type = hidReportRefFeature[1];
      hid_rpt_map[7].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_VAL];
      hid_rpt_map[7].cccdHandle = 0;
      hid_rpt_map[7].mode = HID_PROTOCOL_MODE_REPORT;


  // 向 HID 设备层注册报告映射表
  hid_dev_register_reports(HID_NUM_REPORTS, hid_rpt_map);
}
