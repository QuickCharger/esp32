/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/**
 * @file hid_dev.h
 * @brief HID 设备层头文件
 *
 * 定义了 USB HID 规范中的键盘键码、鼠标按键码、消费者控制码，
 * 以及 HID 报告映射表结构和发送接口。
 *
 * 键码定义参考：USB HID Usage Tables 规范
 */

#ifndef HID_DEV_H__
#define HID_DEV_H__

#include "hidd_le_prf_int.h"


#ifdef __cplusplus
extern "C" {
#endif


/* HID 报告类型 */
#define HID_TYPE_INPUT       1      /*!< 输入报告（设备 → 主机）*/
#define HID_TYPE_OUTPUT      2      /*!< 输出报告（主机 → 设备）*/
#define HID_TYPE_FEATURE     3      /*!< Feature 报告（双向）*/

/* =====================================================================
 *                USB HID 键盘/小键盘 Usage ID 定义
 *     参考：USB HID Usage Tables 规范，Keyboard/Keypad Page (0x07)
 * ===================================================================== */
#define HID_KEY_RESERVED       0    /*!< 保留（无事件）*/
#define HID_KEY_A              4    /*!< 键盘 a 和 A */
#define HID_KEY_B              5    /*!< 键盘 b 和 B */
#define HID_KEY_C              6    /*!< 键盘 c 和 C */
#define HID_KEY_D              7    /*!< 键盘 d 和 D */
#define HID_KEY_E              8    /*!< 键盘 e 和 E */
#define HID_KEY_F              9    /*!< 键盘 f 和 F */
#define HID_KEY_G              10   /*!< 键盘 g 和 G */
#define HID_KEY_H              11   /*!< 键盘 h 和 H */
#define HID_KEY_I              12   /*!< 键盘 i 和 I */
#define HID_KEY_J              13   /*!< 键盘 j 和 J */
#define HID_KEY_K              14   /*!< 键盘 k 和 K */
#define HID_KEY_L              15   /*!< 键盘 l 和 L */
#define HID_KEY_M              16   /*!< 键盘 m 和 M */
#define HID_KEY_N              17   /*!< 键盘 n 和 N */
#define HID_KEY_O              18   /*!< 键盘 o 和 O */
#define HID_KEY_P              19   /*!< 键盘 p 和 P */
#define HID_KEY_Q              20   /*!< 键盘 q 和 Q */
#define HID_KEY_R              21   /*!< 键盘 r 和 R */
#define HID_KEY_S              22   /*!< 键盘 s 和 S */
#define HID_KEY_T              23   /*!< 键盘 t 和 T */
#define HID_KEY_U              24   /*!< 键盘 u 和 U */
#define HID_KEY_V              25   /*!< 键盘 v 和 V */
#define HID_KEY_W              26   /*!< 键盘 w 和 W */
#define HID_KEY_X              27   /*!< 键盘 x 和 X */
#define HID_KEY_Y              28   /*!< 键盘 y 和 Y */
#define HID_KEY_Z              29   /*!< 键盘 z 和 Z */

#define HID_KEY_1              30   /*!< 键盘 1 和 ! */
#define HID_KEY_2              31   /*!< 键盘 2 和 @ */
#define HID_KEY_3              32   /*!< 键盘 3 和 # */
#define HID_KEY_4              33   /*!< 键盘 4 和 $ */
#define HID_KEY_5              34   /*!< 键盘 5 和 % */
#define HID_KEY_6              35   /*!< 键盘 6 和 ^ */
#define HID_KEY_7              36   /*!< 键盘 7 和 & */
#define HID_KEY_8              37   /*!< 键盘 8 和 * */
#define HID_KEY_9              38   /*!< 键盘 9 和 ( */
#define HID_KEY_0              39   /*!< 键盘 0 和 ) */

#define HID_KEY_RETURN         40   /*!< 键盘 Return (ENTER) 回车 */
#define HID_KEY_ESCAPE         41   /*!< 键盘 ESCAPE 退出 */
#define HID_KEY_DELETE         42   /*!< 键盘 DELETE (Backspace) 退格 */
#define HID_KEY_TAB            43   /*!< 键盘 Tab 制表 */
#define HID_KEY_SPACEBAR       44   /*!< 键盘 Spacebar 空格 */
#define HID_KEY_MINUS          45   /*!< 键盘 - 和 _ */
#define HID_KEY_EQUAL          46   /*!< 键盘 = 和 + */
#define HID_KEY_LEFT_BRKT      47   /*!< 键盘 [ 和 { */
#define HID_KEY_RIGHT_BRKT     48   /*!< 键盘 ] 和 } */
#define HID_KEY_BACK_SLASH     49   /*!< 键盘 \\ 和 | */
#define HID_KEY_SEMI_COLON     51   /*!< 键盘 ; 和 : */
#define HID_KEY_SGL_QUOTE      52   /*!< 键盘 ' 和 \" */
#define HID_KEY_GRV_ACCENT     53   /*!< 键盘 ` 和 ~ （重音符/波浪线）*/
#define HID_KEY_COMMA          54   /*!< 键盘 , 和 < */
#define HID_KEY_DOT            55   /*!< 键盘 . 和 > */
#define HID_KEY_FWD_SLASH      56   /*!< 键盘 / 和 ? */
#define HID_KEY_CAPS_LOCK      57   /*!< 键盘 Caps Lock 大写锁定 */

/* 功能键 */
#define HID_KEY_F1             58   /*!< 键盘 F1 */
#define HID_KEY_F2             59   /*!< 键盘 F2 */
#define HID_KEY_F3             60   /*!< 键盘 F3 */
#define HID_KEY_F4             61   /*!< 键盘 F4 */
#define HID_KEY_F5             62   /*!< 键盘 F5 */
#define HID_KEY_F6             63   /*!< 键盘 F6 */
#define HID_KEY_F7             64   /*!< 键盘 F7 */
#define HID_KEY_F8             65   /*!< 键盘 F8 */
#define HID_KEY_F9             66   /*!< 键盘 F9 */
#define HID_KEY_F10            67   /*!< 键盘 F10 */
#define HID_KEY_F11            68   /*!< 键盘 F11 */
#define HID_KEY_F12            69   /*!< 键盘 F12 */

#define HID_KEY_PRNT_SCREEN    70   /*!< 键盘 Print Screen 打印屏幕 */
#define HID_KEY_SCROLL_LOCK    71   /*!< 键盘 Scroll Lock 滚动锁定 */
#define HID_KEY_PAUSE          72   /*!< 键盘 Pause 暂停 */
#define HID_KEY_INSERT         73   /*!< 键盘 Insert 插入 */
#define HID_KEY_HOME           74   /*!< 键盘 Home */
#define HID_KEY_PAGE_UP        75   /*!< 键盘 PageUp 上翻页 */
#define HID_KEY_DELETE_FWD     76   /*!< 键盘 Delete Forward 前向删除 */
#define HID_KEY_END            77   /*!< 键盘 End */
#define HID_KEY_PAGE_DOWN      78   /*!< 键盘 PageDown 下翻页 */

/* 方向键 */
#define HID_KEY_RIGHT_ARROW    79   /*!< 键盘 RightArrow 右箭头 */
#define HID_KEY_LEFT_ARROW     80   /*!< 键盘 LeftArrow 左箭头 */
#define HID_KEY_DOWN_ARROW     81   /*!< 键盘 DownArrow 下箭头 */
#define HID_KEY_UP_ARROW       82   /*!< 键盘 UpArrow 上箭头 */

/* 小键盘 */
#define HID_KEY_NUM_LOCK       83   /*!< 小键盘 Num Lock 和 Clear */
#define HID_KEY_DIVIDE         84   /*!< 小键盘 / */
#define HID_KEY_MULTIPLY       85   /*!< 小键盘 * */
#define HID_KEY_SUBTRACT       86   /*!< 小键盘 - */
#define HID_KEY_ADD            87   /*!< 小键盘 + */
#define HID_KEY_ENTER          88   /*!< 小键盘 ENTER 回车 */
#define HID_KEYPAD_1           89   /*!< 小键盘 1 和 End */
#define HID_KEYPAD_2           90   /*!< 小键盘 2 和 Down Arrow */
#define HID_KEYPAD_3           91   /*!< 小键盘 3 和 PageDn */
#define HID_KEYPAD_4           92   /*!< 小键盘 4 和 Left Arrow */
#define HID_KEYPAD_5           93   /*!< 小键盘 5 */
#define HID_KEYPAD_6           94   /*!< 小键盘 6 和 Right Arrow */
#define HID_KEYPAD_7           95   /*!< 小键盘 7 和 Home */
#define HID_KEYPAD_8           96   /*!< 小键盘 8 和 Up Arrow */
#define HID_KEYPAD_9           97   /*!< 小键盘 9 和 PageUp */
#define HID_KEYPAD_0           98   /*!< 小键盘 0 和 Insert */
#define HID_KEYPAD_DOT         99   /*!< 小键盘 . 和 Delete */

/* 系统控制键（很少直接用作键盘输入，在此列出供参考）*/
#define HID_KEY_MUTE           127  /*!< 键盘 Mute 静音 */
#define HID_KEY_VOLUME_UP      128  /*!< 键盘 Volume up 音量+ */
#define HID_KEY_VOLUME_DOWN    129  /*!< 键盘 Volume down 音量- */

/* 修饰键（用于修饰键字节，也可作为独立键值使用）*/
#define HID_KEY_LEFT_CTRL      224  /*!< 键盘 Left Control 左 Ctrl */
#define HID_KEY_LEFT_SHIFT     225  /*!< 键盘 Left Shift 左 Shift */
#define HID_KEY_LEFT_ALT       226  /*!< 键盘 Left Alt 左 Alt */
#define HID_KEY_LEFT_GUI       227  /*!< 键盘 Left GUI 左 Win/Cmd */
#define HID_KEY_RIGHT_CTRL     228  /*!< 键盘 Right Control 右 Ctrl */
#define HID_KEY_RIGHT_SHIFT    229  /*!< 键盘 Right Shift 右 Shift */
#define HID_KEY_RIGHT_ALT      230  /*!< 键盘 Right Alt 右 Alt */
#define HID_KEY_RIGHT_GUI      231  /*!< 键盘 Right GUI 右 Win/Cmd */

typedef uint8_t keyboard_cmd_t;  /*!< 键盘命令类型 */

/* 鼠标按键 */
#define HID_MOUSE_LEFT       253    /*!< 鼠标左键 */
#define HID_MOUSE_MIDDLE     254    /*!< 鼠标中键 */
#define HID_MOUSE_RIGHT      255    /*!< 鼠标右键 */
typedef uint8_t mouse_cmd_t;       /*!< 鼠标命令类型 */

/* =====================================================================
 *          USB HID 消费者控制（Consumer Control）Usage ID
 *     参考：USB HID Usage Tables 规范，Consumer Page (0x0C)
 * ===================================================================== */

/* 系统控制 */
#define HID_CONSUMER_POWER          48  /*!< 电源 */
#define HID_CONSUMER_RESET          49  /*!< 复位 */
#define HID_CONSUMER_SLEEP          50  /*!< 睡眠 */

/* 菜单与选择 */
#define HID_CONSUMER_MENU           64  /*!< 菜单 */
#define HID_CONSUMER_SELECTION      128 /*!< 选择 */
#define HID_CONSUMER_ASSIGN_SEL     129 /*!< 分配选择 */
#define HID_CONSUMER_MODE_STEP      130 /*!< 模式切换 */
#define HID_CONSUMER_RECALL_LAST    131 /*!< 调用上一个 */
#define HID_CONSUMER_QUIT           148 /*!< 退出 */
#define HID_CONSUMER_HELP           149 /*!< 帮助 */

/* 频道 */
#define HID_CONSUMER_CHANNEL_UP     156 /*!< 频道+ */
#define HID_CONSUMER_CHANNEL_DOWN   157 /*!< 频道- */

/* 媒体播放控制 */
#define HID_CONSUMER_PLAY           176 /*!< 播放 */
#define HID_CONSUMER_PAUSE          177 /*!< 暂停 */
#define HID_CONSUMER_RECORD         178 /*!< 录制 */
#define HID_CONSUMER_FAST_FORWARD   179 /*!< 快进 */
#define HID_CONSUMER_REWIND         180 /*!< 快退 */
#define HID_CONSUMER_SCAN_NEXT_TRK  181 /*!< 下一曲 */
#define HID_CONSUMER_SCAN_PREV_TRK  182 /*!< 上一曲 */
#define HID_CONSUMER_STOP           183 /*!< 停止 */
#define HID_CONSUMER_EJECT          184 /*!< 弹出 */
#define HID_CONSUMER_RANDOM_PLAY    185 /*!< 随机播放 */
#define HID_CONSUMER_SELECT_DISC    186 /*!< 选择光盘 */
#define HID_CONSUMER_ENTER_DISC     187 /*!< 装入光盘 */
#define HID_CONSUMER_REPEAT         188 /*!< 重复 */
#define HID_CONSUMER_STOP_EJECT     204 /*!< 停止/弹出 */
#define HID_CONSUMER_PLAY_PAUSE     205 /*!< 播放/暂停 */
#define HID_CONSUMER_PLAY_SKIP      206 /*!< 播放/跳过 */

/* 音量与音频 */
#define HID_CONSUMER_VOLUME         224 /*!< 音量 */
#define HID_CONSUMER_BALANCE        225 /*!< 平衡 */
#define HID_CONSUMER_MUTE           226 /*!< 静音 */
#define HID_CONSUMER_BASS           227 /*!< 低音 */
#define HID_CONSUMER_VOLUME_UP      233 /*!< 音量+ */
#define HID_CONSUMER_VOLUME_DOWN    234 /*!< 音量- */

typedef uint8_t consumer_cmd_t;      /*!< 消费者控制命令类型 */

/* =====================================================================
 *          Consumer Control 报告中的按钮索引编码
 *
 * Consumer Control 报告为 2 字节格式：
 *   Byte 0: [Channel(2bit)] [Volume Up(1bit)] [Volume Down(1bit)] [Numeric(4bit)]
 *   Byte 1: [Selection(2bit)] [Button(4bit)] [Reserved(2bit)]
 * ===================================================================== */

/* Byte 1 中的按钮编码（低 4 位）*/
#define HID_CC_RPT_MUTE                 1    /*!< 静音按钮 */
#define HID_CC_RPT_POWER                2    /*!< 电源按钮 */
#define HID_CC_RPT_LAST                 3    /*!< 上一个 */
#define HID_CC_RPT_ASSIGN_SEL           4    /*!< 分配选择 */
#define HID_CC_RPT_PLAY                 5    /*!< 播放 */
#define HID_CC_RPT_PAUSE                6    /*!< 暂停 */
#define HID_CC_RPT_RECORD               7    /*!< 录制 */
#define HID_CC_RPT_FAST_FWD             8    /*!< 快进 */
#define HID_CC_RPT_REWIND               9    /*!< 快退 */
#define HID_CC_RPT_SCAN_NEXT_TRK        10   /*!< 下一曲 */
#define HID_CC_RPT_SCAN_PREV_TRK        11   /*!< 上一曲 */
#define HID_CC_RPT_STOP                 12   /*!< 停止 */

/* Byte 0 中的频道/音量编码 */
#define HID_CC_RPT_CHANNEL_UP           0x01 /*!< 频道+（bit4-5）*/
#define HID_CC_RPT_CHANNEL_DOWN         0x03 /*!< 频道-（bit4-5）*/
#define HID_CC_RPT_VOLUME_UP            0x40 /*!< 音量+（bit6）*/
#define HID_CC_RPT_VOLUME_DOWN          0x80 /*!< 音量-（bit7）*/

/* Consumer Control 报告位掩码（用于清除指定位域）*/
#define HID_CC_RPT_NUMERIC_BITS         0xF0 /*!< Byte 0 低 4 位：数字键区 */
#define HID_CC_RPT_CHANNEL_BITS         0xCF /*!< Byte 0 bit4-5：频道 */
#define HID_CC_RPT_VOLUME_BITS          0x3F /*!< Byte 0 bit6-7：音量 */
#define HID_CC_RPT_BUTTON_BITS          0xF0 /*!< Byte 1 低 4 位：按钮 */
#define HID_CC_RPT_SELECTION_BITS       0xCF /*!< Byte 1 bit4-5：选择 */


/**
 * @defgroup CC_Macros Consumer Control 报告构建宏
 *
 * 这些宏用于在 Consumer Control 的 2 字节报告缓冲区中设置各个字段。
 * 用法：先清零缓冲区，然后调用相应宏设置需要的值。
 *
 * @param s  报告缓冲区（uint8_t[2]）
 * @param x  要设置的值
 * @{
 */
#define HID_CC_RPT_SET_NUMERIC(s, x)    (s)[0] &= HID_CC_RPT_NUMERIC_BITS;   \
                                        (s)[0] = (x)                          /*!< 设置数字键区值 */

#define HID_CC_RPT_SET_CHANNEL(s, x)    (s)[0] &= HID_CC_RPT_CHANNEL_BITS;   \
                                        (s)[0] |= ((x) & 0x03) << 4          /*!< 设置频道值 */

#define HID_CC_RPT_SET_VOLUME_UP(s)     (s)[0] &= HID_CC_RPT_VOLUME_BITS;    \
                                        (s)[0] |= 0x40                        /*!< 设置音量+ */

#define HID_CC_RPT_SET_VOLUME_DOWN(s)   (s)[0] &= HID_CC_RPT_VOLUME_BITS;    \
                                        (s)[0] |= 0x80                        /*!< 设置音量- */

#define HID_CC_RPT_SET_BUTTON(s, x)     (s)[1] &= HID_CC_RPT_BUTTON_BITS;    \
                                        (s)[1] |= (x)                         /*!< 设置按钮值 */

#define HID_CC_RPT_SET_SELECTION(s, x)  (s)[1] &= HID_CC_RPT_SELECTION_BITS; \
                                        (s)[1] |= ((x) & 0x03) << 4          /*!< 设置选择值 */
/** @} */


/* =====================================================================
 *                      数据结构与 API 函数
 * ===================================================================== */

/**
 * @brief HID 报告映射表条目
 *
 * 将报告 ID、类型和协议模式映射到对应的 GATT Characteristic 句柄。
 * 发送报告时通过此表查找目标 Characteristic。
 */
typedef struct
{
  uint16_t    handle;           /*!< Report Characteristic 的 GATT 句柄 */
  uint16_t    cccdHandle;       /*!< CCCD 的 GATT 句柄 */
  uint8_t     id;               /*!< 报告 ID */
  uint8_t     type;             /*!< 报告类型（Input/Output/Feature）*/
  uint8_t     mode;             /*!< 协议模式（Boot 或 Report）*/
} hid_report_map_t;

/**
 * @brief HID 设备配置结构体
 */
typedef struct
{
  uint32_t    idleTimeout;      /*!< 空闲超时时间（毫秒）*/
  uint8_t     hidFlags;         /*!< HID 特性标志 */

} hid_dev_cfg_t;

/**
 * @brief 注册 HID 报告映射表
 * @param num_reports  报告数量
 * @param p_report     报告映射表数组指针
 */
void hid_dev_register_reports(uint8_t num_reports, hid_report_map_t *p_report);

/**
 * @brief 发送 HID 报告（通过 GATT Indication）
 * @param gatts_if  GATT 接口句柄
 * @param conn_id   连接 ID
 * @param id        报告 ID
 * @param type      报告类型
 * @param length    数据长度
 * @param data      数据指针
 */
void hid_dev_send_report(esp_gatt_if_t gatts_if, uint16_t conn_id,
                                    uint8_t id, uint8_t type, uint8_t length, uint8_t *data);

/**
 * @brief 构建消费者控制报告
 * @param buffer  2 字节报告缓冲区
 * @param cmd     消费者控制命令
 */
void hid_consumer_build_report(uint8_t *buffer, consumer_cmd_t cmd);

/**
 * @brief 构建键盘报告（预留接口）
 */
void hid_keyboard_build_report(uint8_t *buffer, keyboard_cmd_t cmd);

/**
 * @brief 构建鼠标报告（预留接口）
 */
void hid_mouse_build_report(uint8_t *buffer, mouse_cmd_t cmd);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* HID_DEV_H__ */
