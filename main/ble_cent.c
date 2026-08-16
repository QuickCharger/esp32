/*
 * BLE Central 模块 — 扫描/连接键盘鼠标
 *
 * 命令通过 Web Bluetooth Data Char 发送，结果通过 web_ble_config_log 推送。
 */

#include "ble_cent.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_main.h"
#include "web_ble_config.h"

#define GATTC_APP_ID  0x6666

/* 外部广播控制函数（在 ble_hidd_demo_main.c 中定义）*/
extern void hidd_adv_stop(void);
extern void hidd_adv_start(void);
extern void hidd_forward_mouse(uint8_t mouse_button, int8_t mickeys_x, int8_t mickeys_y, int8_t wheel, int8_t ac_pan);
#define SCAN_DURATION 10  /* 扫描持续秒数 */

/* BLE HID 相关 UUID */
#define HID_SVC_UUID_16      0x1812   /* HID 服务 */
#define HID_REPORT_CHAR_16   0x2A4D   /* HID Report Characteristic */
#define HID_REPORT_MAP_16    0x2A4B   /* HID Report Map（报告格式描述符）*/
#define CCCD_UUID_16         0x2902   /* Client Characteristic Configuration Descriptor */

static esp_gatt_if_t g_gattc_if = ESP_GATT_IF_NONE;
static bool g_scanning = false;
static int  g_scan_count = 0;

/* 鼠标事件队列（解耦 BLE 回调与发送，避免回调中直接发送导致崩溃）*/
typedef struct {
    uint8_t button;   /* 按键位图，bit0=左键 bit1=右键 bit2=中键 bit3~7=扩展按键 */
    int8_t  x;
    int8_t  y;
    int8_t  wheel;    /* 垂直滚轮 */
    int8_t  pan;      /* 水平滚轮 AC Pan */
} mouse_event_t;

static QueueHandle_t g_mouse_queue = NULL;
static void mouse_send_task(void *param);

/* Report Map 解析结果（自动从描述符提取，非硬编码）*/
static int g_x_bit_offset = -1, g_x_size = 0;
static int g_y_bit_offset = -1, g_y_size = 0;
static int g_wheel_bit_offset = -1, g_wheel_size = 0;
static int g_pan_bit_offset = -1, g_pan_size = 0;   /* 水平滚轮 AC Pan（Usage 0x238）*/
static int g_btn_bit_offset = -1, g_btn_size = 0;
static int g_mouse_report_len = 0;   /* 鼠标报告字节数（由 Report Map 计算，用于过滤 vendor 报告）*/

static void parse_report_map(const uint8_t *data, int len);
static int extract_field(const uint8_t *report, int len, int bit_offset, int size);
static int8_t clamp_to_int8(int raw);
static uint16_t g_conn_id = 0;
static uint16_t g_hid_svc_start = 0;
static uint16_t g_hid_svc_end = 0;
#define MAX_REPORT_CHARS 10

static uint16_t g_report_char_handles[MAX_REPORT_CHARS] = {0};
static uint16_t g_report_cccd_handles[MAX_REPORT_CHARS] = {0};
static uint8_t  g_report_char_count = 0;
static esp_bd_addr_t g_server_bda = {0};
static bool     g_cccd_task_created = false;   /* 防止重复创建任务 */

static void cccd_write_task(void *param);

/* =====================================================================
 *                    GAP 事件回调（扫描结果）
 * ===================================================================== */

void ble_cent_gap_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        web_ble_config_log("[CENT] 扫描参数设置完成，开始扫描 %d 秒...", SCAN_DURATION);
        esp_ble_gap_start_scanning(SCAN_DURATION);
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        web_ble_config_log("[CENT] 扫描已启动");
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *r = (esp_ble_gap_cb_param_t *)param;
        if (r->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            g_scan_count++;

            // 获取设备名称
            uint8_t *name = NULL;
            uint8_t name_len = 0;
            name = esp_ble_resolve_adv_data_by_type(
                r->scan_rst.ble_adv,
                r->scan_rst.adv_data_len + r->scan_rst.scan_rsp_len,
                ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
            if (!name) {
                name = esp_ble_resolve_adv_data_by_type(
                    r->scan_rst.ble_adv,
                    r->scan_rst.adv_data_len + r->scan_rst.scan_rsp_len,
                    ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
            }

            char dev_name[32] = "(未知)";
            if (name && name_len > 0 && name_len < 32) {
                memcpy(dev_name, name, name_len);
                dev_name[name_len] = '\0';
            }

            int rssi = r->scan_rst.rssi;
            int addr_type = r->scan_rst.ble_addr_type;  // 0=public, 1=random

            // 结构化输出：DEV|地址(无冒号)|类型|RSSI|名称，方便 HTML 解析
            web_ble_config_log("DEV|%02x%02x%02x%02x%02x%02x|%d|%d|%s",
                               r->scan_rst.bda[0], r->scan_rst.bda[1],
                               r->scan_rst.bda[2], r->scan_rst.bda[3],
                               r->scan_rst.bda[4], r->scan_rst.bda[5],
                               addr_type, rssi, dev_name);
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        g_scanning = false;
        web_ble_config_log("[CENT] 扫描完成, 共发现 %d 个设备", g_scan_count);
        hidd_adv_start();   /* 恢复广播 */
        break;

    default:
        break;
    }
}

/* =====================================================================
 *                    GATT Client 事件回调
 * ===================================================================== */

static void ble_cent_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                               esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            g_gattc_if = gattc_if;
            web_ble_config_log("[CENT] GATT Client 注册成功");
        } else {
            web_ble_config_log("[CENT] GATT Client 注册失败! status=%d", param->reg.status);
        }
        break;

    case ESP_GATTC_CONNECT_EVT:
        if (param->connect.link_role == 0) {  // 0 = Central
            web_ble_config_log("[CENT] 已连接设备 %02x%02x%02x%02x%02x%02x, conn_id=%d",
                param->connect.remote_bda[0], param->connect.remote_bda[1],
                param->connect.remote_bda[2], param->connect.remote_bda[3],
                param->connect.remote_bda[4], param->connect.remote_bda[5],
                param->connect.conn_id);
        }
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        web_ble_config_log("[CENT] 设备断开, reason=0x%02x", param->disconnect.reason);
        g_cccd_task_created = false;   // 重置标志，下次连接可重新创建任务
        g_conn_id = 0;
        break;

    case ESP_GATTC_OPEN_EVT:
        web_ble_config_log("[CENT] 连接打开, status=%d, conn_id=%d",
                           param->open.status, param->open.conn_id);
        if (param->open.status == ESP_GATT_OK) {
            // 连接建立后停止广播，释放射频给连接
            hidd_adv_stop();
            // 启动服务发现
            esp_ble_gattc_search_service(gattc_if, param->open.conn_id, NULL);
        }
        break;

    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        web_ble_config_log("[CENT] 服务发现完成, conn_id=%d, 搜索 HID 服务...", param->dis_srvc_cmpl.conn_id);
        g_conn_id = param->dis_srvc_cmpl.conn_id;
        // 搜索 HID 服务 (0x1812)
        {
            esp_bt_uuid_t hid_uuid = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = HID_SVC_UUID_16};
            esp_ble_gattc_search_service(gattc_if, g_conn_id, &hid_uuid);
        }
        break;

    case ESP_GATTC_SEARCH_RES_EVT:
        // 服务搜索结果：找到 HID 服务
        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 &&
            param->search_res.srvc_id.uuid.uuid.uuid16 == HID_SVC_UUID_16) {
            g_hid_svc_start = param->search_res.start_handle;
            g_hid_svc_end = param->search_res.end_handle;
            web_ble_config_log("[CENT] 找到 HID 服务: handle %d~%d", g_hid_svc_start, g_hid_svc_end);

            // 读取 Report Map 描述符 (0x2A4B)，用于精确解析报告格式
            esp_gattc_char_elem_t map_elem;
            uint16_t map_count = 1;
            esp_bt_uuid_t map_uuid = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = HID_REPORT_MAP_16};
            esp_err_t map_err = esp_ble_gattc_get_char_by_uuid(gattc_if, g_conn_id,
                                                               g_hid_svc_start, g_hid_svc_end,
                                                               map_uuid, &map_elem, &map_count);
            if (map_err == ESP_GATT_OK && map_count > 0) {
                web_ble_config_log("[CENT] 找到 Report Map: value handle=%d, 读取描述符...", map_elem.char_handle);
                // char_handle 是 value handle，直接读，不需要 +1
                esp_ble_gattc_read_char(gattc_if, g_conn_id, map_elem.char_handle,
                                        ESP_GATT_AUTH_REQ_NONE);
            }

            // 搜索所有 Report Characteristic (0x2A4D)，最多 MAX_REPORT_CHARS 个
            esp_gattc_char_elem_t char_elems[MAX_REPORT_CHARS];
            uint16_t count = MAX_REPORT_CHARS;
            esp_bt_uuid_t report_uuid = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = HID_REPORT_CHAR_16};
            esp_err_t err = esp_ble_gattc_get_char_by_uuid(gattc_if, g_conn_id,
                                                           g_hid_svc_start, g_hid_svc_end,
                                                           report_uuid, char_elems, &count);
            if (err == ESP_GATT_OK && count > 0) {
                web_ble_config_log("[CENT] 找到 %d 个 Report Char", count);
                g_report_char_count = 0;
                for (int i = 0; i < count && i < MAX_REPORT_CHARS; i++) {
                    g_report_char_handles[i] = char_elems[i].char_handle;
                    // 搜索该 Char 的 CCCD 描述符
                    esp_gattc_descr_elem_t descr_elem;
                    uint16_t descr_count = 1;
                    esp_bt_uuid_t cccd_uuid = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = CCCD_UUID_16};
                    esp_gatt_status_t descr_err = esp_ble_gattc_get_descr_by_char_handle(
                        gattc_if, g_conn_id, char_elems[i].char_handle, cccd_uuid,
                        &descr_elem, &descr_count);
                    if (descr_err == ESP_GATT_OK && descr_count > 0) {
                        g_report_cccd_handles[i] = descr_elem.handle;
                        web_ble_config_log("[CENT] Report[%d]: decl=%d, CCCD=%d",
                                           i, char_elems[i].char_handle, descr_elem.handle);
                    } else {
                        g_report_cccd_handles[i] = 0;
                    }
                }
                g_report_char_count = count;

                if (!g_cccd_task_created) {
                    g_cccd_task_created = true;
                    web_ble_config_log("[CENT] 延迟3秒后启用所有 Report 通知...");
                    xTaskCreate(cccd_write_task, "cccd_write", 4096, NULL, 5, NULL);
                }
            }
        }
        break;

    case ESP_GATTC_WRITE_DESCR_EVT:
        web_ble_config_log("[CENT] CCCD 写入完成, status=%d", param->write.status);
        if (param->write.status == ESP_GATT_OK) {
            web_ble_config_log("[CENT] 通知已启用！移动鼠标或按键应能看到报告");
        } else {
            // 写入失败（可能加密未就绪），延迟重试
            web_ble_config_log("[CENT] CCCD 写入失败，2秒后重试...");
            xTaskCreate(cccd_write_task, "cccd_retry", 2048, NULL, 5, NULL);
        }
        break;

    case ESP_GATTC_WRITE_CHAR_EVT:
        web_ble_config_log("[CENT] 写入完成, conn_id=%d, status=%d", param->write.conn_id, param->write.status);
        break;

    case ESP_GATTC_READ_CHAR_EVT:
        // 读取到 Report Map 描述符，自动解析字段布局
        if (param->read.status == ESP_GATT_OK) {
            web_ble_config_log("[CENT] Report Map 长度=%d, 自动解析...", param->read.value_len);
            ESP_LOG_BUFFER_HEX("BLE_CENT", param->read.value, param->read.value_len);
            parse_report_map(param->read.value, param->read.value_len);
        }
        break;

    case ESP_GATTC_NOTIFY_EVT: {
        // 收到 HID 报告
        uint8_t *data = param->notify.value;
        uint16_t len = param->notify.value_len;
        uint16_t handle = param->notify.handle;

        // 限流调试日志：每 100 个通知打印一次摘要，避免高频串口阻塞导致死机
        static uint32_t notify_count = 0;
        notify_count++;
        if ((notify_count % 100) == 1) {
            web_ble_config_log("[CENT] 通知#%lu handle=%d len=%d 鼠标报告=%d字节",
                               (unsigned long)notify_count, handle, len, g_mouse_report_len);
        }

        // 只解析鼠标报告：按报告长度过滤掉其他报告（如罗技 vendor 报告 19 字节）
        if (g_mouse_report_len <= 0 || len != g_mouse_report_len) {
            break;
        }

        // 根据 Report Map 解析结果提取字段（自动，非硬编码）
        if (g_mouse_queue && g_x_bit_offset >= 0 && g_y_bit_offset >= 0) {
            mouse_event_t ev;
            int raw_x = extract_field(data, len, g_x_bit_offset, g_x_size);
            int raw_y = extract_field(data, len, g_y_bit_offset, g_y_size);
            int raw_btn = extract_field(data, len, g_btn_bit_offset, g_btn_size);
            int raw_wheel = extract_field(data, len, g_wheel_bit_offset, g_wheel_size);
            int raw_pan = extract_field(data, len, g_pan_bit_offset, g_pan_size);

            ev.button = (uint8_t)raw_btn;   // 完整按钮位图（支持 8 个按钮）
            ev.x = clamp_to_int8(raw_x);
            ev.y = clamp_to_int8(raw_y);
            ev.wheel = clamp_to_int8(raw_wheel);
            ev.pan = clamp_to_int8(raw_pan);

            // 限流解析日志
            if ((notify_count % 100) == 1) {
                web_ble_config_log("[CENT] 解析 btn=0x%x x=%d y=%d wheel=%d pan=%d",
                                   ev.button, ev.x, ev.y, ev.wheel, ev.pan);
            }

            // 非阻塞入队，队列满则丢弃（限流）
            xQueueSend(g_mouse_queue, &ev, 0);
        }
        break;
    }

    default:
        break;
    }
}

/**
 * @brief 鼠标数据发送任务（从队列取数据转发给电脑，避免回调中直接发送）
 */
static void mouse_send_task(void *param)
{
    mouse_event_t ev;
    while (1) {
        if (xQueueReceive(g_mouse_queue, &ev, portMAX_DELAY) == pdTRUE) {
            hidd_forward_mouse(ev.button, ev.x, ev.y, ev.wheel, ev.pan);
        }
    }
}

/**
 * @brief 将鼠标移动增量截断到 int8 范围
 *
 * HID 鼠标报告的 X/Y 是相对位移增量（通常 1~几十），
 * 直接截断到 int8 即可，不做比例缩放（缩放会把小增量归零）。
 */
static int8_t clamp_to_int8(int raw)
{
    if (raw > 127) return 127;
    if (raw < -127) return -127;
    return (int8_t)raw;
}

/**
 * @brief 从报告中提取指定位偏移的字段值（支持 1~16 位，有符号/无符号）
 */
static int extract_field(const uint8_t *report, int len, int bit_offset, int size)
{
    if (bit_offset < 0 || size <= 0) return 0;
    if ((bit_offset + size) > len * 8) return 0;

    int value = 0;
    for (int i = 0; i < size; i++) {
        int bit_pos = bit_offset + i;
        int byte_idx = bit_pos / 8;
        int bit_in_byte = bit_pos % 8;
        if (report[byte_idx] & (1 << bit_in_byte)) {
            value |= (1 << i);
        }
    }

    // 符号扩展
    if (size < 16 && (value & (1 << (size - 1)))) {
        value |= ~((1 << size) - 1);
    }
    return value;
}

/**
 * @brief 解析 HID Report Descriptor，自动提取 X/Y/滚轮/按键的字段位置
 *
 * 这是标准 HID Report Descriptor 解析器，非硬编码。
 * 遍历描述符 item，跟踪 Usage/Report Size/Count/Input，计算每个字段的 bit offset。
 */
static void parse_report_map(const uint8_t *data, int len)
{
    uint16_t usage_page = 0;
    uint16_t usages[8] = {0};
    int usage_count = 0;
    int usage_max = 0;
    int report_size = 0;
    int report_count = 0;
    int bit_offset = 0;          /* 当前报告内的位偏移 */
    int report_id = 0;           /* 当前 Report ID（描述符未声明 ID 时为 0）*/
    int mouse_report_id = -1;    /* 包含鼠标字段（按钮/X/Y/滚轮）的 Report ID */
    int report_bits = 0;         /* 当前报告已累计的位数 */
    int mouse_report_bits = 0;   /* 鼠标报告的总位数 */

    int i = 0;
    while (i < len) {
        uint8_t header = data[i++];
        int item_size = header & 0x03;
        int item_type = (header >> 2) & 0x03;
        int item_tag = (header >> 4) & 0x0F;
        if (item_size == 3) item_size = 4;

        uint32_t val = 0;
        for (int j = 0; j < item_size && i < len; j++) {
            val |= ((uint32_t)data[i++]) << (j * 8);
        }

        if (item_type == 0) {  /* Main item */
            if (item_tag == 0x8) {  /* Input (0x81) */
                bool is_mouse_input = false;

                /* X/Y/Wheel/AC Pan 字段（Generic Desktop Page 0x01 + 固定 Usage ID）*/
                for (int u = 0; u < usage_count; u++) {
                    uint16_t usage = usages[u];
                    int offset = bit_offset + u * report_size;  /* 每个 usage 递增一个字段宽度 */
                    if (usage_page == 0x01 && usage == 0x30) {
                        g_x_bit_offset = offset; g_x_size = report_size;
                        is_mouse_input = true;
                    } else if (usage_page == 0x01 && usage == 0x31) {
                        g_y_bit_offset = offset; g_y_size = report_size;
                        is_mouse_input = true;
                    } else if (usage_page == 0x01 && usage == 0x38) {
                        g_wheel_bit_offset = offset; g_wheel_size = report_size;
                        is_mouse_input = true;
                    } else if (usage_page == 0x01 && usage == 0x238) {
                        g_pan_bit_offset = offset; g_pan_size = report_size;
                        is_mouse_input = true;
                    }
                }

                /* 按钮字段（Button Page 0x09 + Usage Min/Max）*/
                if (usage_page == 0x09 && usage_max > 0 && g_btn_bit_offset < 0) {
                    g_btn_bit_offset = bit_offset;
                    g_btn_size = report_size * report_count;  /* 完整按钮位图 */
                    is_mouse_input = true;
                }

                /* 该 Input 属于鼠标报告 → 记录其 Report ID */
                if (is_mouse_input && mouse_report_id < 0) {
                    mouse_report_id = report_id;
                }

                /* 累计当前报告的位数 */
                bit_offset += report_size * report_count;
                report_bits += report_size * report_count;
                usage_count = 0;
                usage_max = 0;
            }
        } else if (item_type == 1) {  /* Global item */
            switch (item_tag) {
                case 0x0: usage_page = (uint16_t)val; break;  /* Usage Page */
                case 0x7: report_size = (int)val; break;       /* Report Size */
                case 0x9: report_count = (int)val; break;      /* Report Count */
                case 0x8:  /* Report ID：新报告开始，字段从 bit 0 重新计 */
                    /* 上一个报告若是鼠标报告，保存其位数 */
                    if (report_id == mouse_report_id) {
                        mouse_report_bits = report_bits;
                    }
                    report_id = (int)val;
                    bit_offset = 0;
                    report_bits = 0;
                    break;
                default: break;
            }
        } else if (item_type == 2) {  /* Local item */
            switch (item_tag) {
                case 0x0:  /* Usage */
                    if (usage_count < 8) usages[usage_count++] = (uint16_t)val;
                    break;
                case 0x2: usage_max = (int)val; break;  /* Usage Maximum */
                default: break;
            }
        }
    }

    /* 循环结束：处理最后一个报告 */
    if (report_id == mouse_report_id) {
        mouse_report_bits = report_bits;
    }

    /* 鼠标报告长度 = 鼠标报告位数向上取整到字节 */
    g_mouse_report_len = (mouse_report_bits + 7) / 8;

    web_ble_config_log("[CENT] Report Map 解析: X=%d(%dbit) Y=%d(%dbit) Wheel=%d(%dbit) Pan=%d(%dbit) Btn=%d(%dbit) 鼠标报告ID=%d 长度=%d字节",
                       g_x_bit_offset, g_x_size, g_y_bit_offset, g_y_size,
                       g_wheel_bit_offset, g_wheel_size, g_pan_bit_offset, g_pan_size,
                       g_btn_bit_offset, g_btn_size,
                       mouse_report_id, g_mouse_report_len);
}

/**
 * @brief 延迟写 CCCD 的任务（不依赖配对事件）
 */
static void cccd_write_task(void *param)
{
    vTaskDelay(3000 / portTICK_PERIOD_MS);  // 等待配对/加密完成

    if (g_conn_id == 0 || g_report_char_count == 0) {
        vTaskDelete(NULL);
        return;
    }

    // 遍历所有 Report Char，逐个注册通知并写 CCCD
    for (int i = 0; i < g_report_char_count && i < MAX_REPORT_CHARS; i++) {
        if (g_report_cccd_handles[i] == 0) continue;

        // 先注册通知
        esp_err_t reg_err = esp_ble_gattc_register_for_notify(g_gattc_if, g_server_bda, g_report_char_handles[i]);
        web_ble_config_log("[CENT] Report[%d] 注册通知 handle=%d, ret=%d",
                           i, g_report_char_handles[i], reg_err);

        // 写 CCCD = 0x0001 (Notification)
        uint8_t cccd_val[2] = {0x01, 0x00};
        esp_ble_gattc_write_char_descr(g_gattc_if, g_conn_id,
                                       g_report_cccd_handles[i],
                                       sizeof(cccd_val), cccd_val,
                                       ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
        web_ble_config_log("[CENT] Report[%d] 写 CCCD handle=%d", i, g_report_cccd_handles[i]);
        vTaskDelay(200 / portTICK_PERIOD_MS);  // 间隔，避免操作冲突
    }
    web_ble_config_log("[CENT] 所有 Report 通知已启用");
    vTaskDelete(NULL);
}

void ble_cent_connect(const char *addr_str, uint8_t addr_type)
{
    esp_bd_addr_t addr;
    // 解析 "aabbccddeeff" 格式地址
    for (int i = 0; i < 6; i++) {
        char byte_str[3] = {addr_str[i*2], addr_str[i*2+1], '\0'};
        addr[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }
    memcpy(g_server_bda, addr, sizeof(esp_bd_addr_t));  // 保存远程地址供 register_for_notify 使用

    web_ble_config_log("[CENT] 正在连接设备 %s (type=%d)...", addr_str, addr_type);

    // 自定义连接参数：增大监督超时，避免射频忙时 0x08 超时断开
    esp_ble_conn_params_t conn_params = {
        .scan_interval = 0x40,          // 40 * 0.625ms = 25ms
        .scan_window = 0x20,            // 20 * 0.625ms = 12.5ms
        .interval_min = 24,             // 24 * 1.25ms = 30ms 连接间隔
        .interval_max = 40,             // 40 * 1.25ms = 50ms 连接间隔
        .latency = 0,                   // 从设备延迟
        .supervision_timeout = 600,     // 600 * 10ms = 6秒 监督超时
        .min_ce_len = 0,
        .max_ce_len = 0,
    };

    esp_ble_gatt_creat_conn_params_t creat_conn_params = {
        .remote_bda = {0},
        .remote_addr_type = addr_type,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .is_direct = true,
        .is_aux = false,
        .phy_mask = ESP_BLE_PHY_1M_PREF_MASK,
        .phy_1m_conn_params = &conn_params,
        .phy_2m_conn_params = NULL,
        .phy_coded_conn_params = NULL,
    };
    memcpy(creat_conn_params.remote_bda, addr, sizeof(esp_bd_addr_t));

    esp_ble_gattc_enh_open(g_gattc_if, &creat_conn_params);
}

/* =====================================================================
 *                    公开 API
 * ===================================================================== */

void ble_cent_init(void)
{
    // 创建鼠标事件队列和发送任务（解耦 BLE 回调）
    g_mouse_queue = xQueueCreate(64, sizeof(mouse_event_t));
    if (g_mouse_queue) {
        xTaskCreate(mouse_send_task, "mouse_send", 4096, NULL, 5, NULL);
    }

    esp_err_t ret = esp_ble_gattc_register_callback(ble_cent_gattc_cb);
    if (ret != ESP_OK) {
        web_ble_config_log("[CENT] 注册 GATTC 回调失败: %d", ret);
        return;
    }
    ret = esp_ble_gattc_app_register(GATTC_APP_ID);
    if (ret != ESP_OK) {
        web_ble_config_log("[CENT] 注册 GATTC App 失败: %d", ret);
    }
}

void ble_cent_start_scan(void)
{
    if (g_scanning) {
        web_ble_config_log("[CENT] 扫描正在进行中...");
        return;
    }
    g_scanning = true;
    g_scan_count = 0;
    hidd_adv_stop();   /* 暂停广播，释放射频给扫描 */
    web_ble_config_log("[CENT] 开始扫描 BLE 设备...（已暂停广播）");

    // 配置扫描参数
    esp_ble_scan_params_t scan_params = {
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,   /* 使用公共地址，避免随机地址未设置 */
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_type = BLE_SCAN_TYPE_ACTIVE,   /* 主动扫描，可获取设备名称 */
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,  /* 关闭去重，避免漏报 */
        .scan_interval = 160,                /* 100ms 扫描间隔 */
        .scan_window = 160,                  /* 100% 占空比，提高扫描灵敏度 */
    };

    esp_err_t ret = esp_ble_gap_set_scan_params(&scan_params);
    if (ret != ESP_OK) {
        g_scanning = false;
        web_ble_config_log("[CENT] 设置扫描参数失败: %d", ret);
    }
    // 等待 ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT 后自动开始扫描
}
