#include "ui.h"
#include "core/lv_obj.h"
#include "core/lv_obj_pos.h"
#include "misc/lv_types.h"
#include "lv_port_indev.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "tim.h"
#include "usart.h"
#include "draw/lv_draw_line.h"
#include "stm32h7xx_hal_iwdg.h"

/* ★ 声明 freertos.c 中定义的共享变量 */
extern volatile uint32_t g_adc_raw[5];

/* 声明外部的图片资源 */
LV_IMAGE_DECLARE(photo_01);

/* ================================================================
 *  全局变量
 * ================================================================ */

/* 按钮指针 */
static lv_obj_t * btn[9];

/* 调试标签 */
static lv_obj_t * debug_label = NULL;

/* ★ 所有页面的指针 */
static lv_obj_t * scr_main = NULL;
static lv_obj_t * scr_adc  = NULL;
static lv_obj_t * scr_dac  = NULL;
static lv_obj_t * scr_pwm  = NULL;
static lv_obj_t * scr_uart = NULL;
static lv_obj_t * scr_task = NULL;

/* ★ ADC 页面的数值标签 */
static lv_obj_t * adc_labels[5] = {NULL};

/* ★ DAC 页面的控件 */
static lv_obj_t * dac_canvas = NULL;
static lv_obj_t * dac_voltage_label = NULL;
static uint32_t   dac_voltage_mv = 1650;  // 初始 1.650V
#define DAC_CANVAS_W  200
#define DAC_CANVAS_H  100
__attribute__((section(".ram_d3")))
static uint8_t dac_canvas_buf[DAC_CANVAS_W * DAC_CANVAS_H];

/* ★ PWM 页面的控件 */
static lv_obj_t * pwm_canvas = NULL;
static lv_obj_t * pwm_duty_label = NULL;
static uint32_t   pwm_duty = 50;  // 初始 50%
#define PWM_CANVAS_W  200
#define PWM_CANVAS_H  100
__attribute__((section(".ram_d3")))
static uint8_t pwm_canvas_buf[PWM_CANVAS_W * PWM_CANVAS_H];

/* ★ UART 页面的控件 */
static lv_obj_t * uart_rx_label = NULL;
static lv_obj_t * uart_info_label = NULL;
static uint32_t   uart_rx_count = 0;
#define UART_DISP_BUF_SIZE 256
static char uart_disp_buf[UART_DISP_BUF_SIZE];

/* ★ TASK 页面的控件 */
static lv_obj_t * task_switches[6] = {NULL};
static bool task_enabled[6] = {false};

/* ★ SET 页面的控件 */
static lv_obj_t * scr_set = NULL;
static lv_obj_t * set_dropdown = NULL;
static lv_obj_t * set_preview_img = NULL;
static lv_obj_t * wdg_switch = NULL;  /* 看门狗开关 */
static lv_obj_t * wdg_status_label = NULL;  /* 看门狗状态标签 */

/* ★ 看门狗相关 */
static IWDG_HandleTypeDef hiwdg;
static bool wdg_enabled = false;  /* 看门狗是否启用 */

/* ★ 背景图片资源 */
LV_IMAGE_DECLARE(photo_01);
LV_IMAGE_DECLARE(photo_02);
LV_IMAGE_DECLARE(photo_03);

/* 背景图片数组（用于切换） */
static const lv_img_dsc_t * bg_images[] = {
    &photo_01,
    &photo_02,
    &photo_03,
};
#define BG_IMAGE_COUNT (sizeof(bg_images) / sizeof(bg_images[0]))

/* 当前背景图片索引 */
static uint32_t current_bg_index = 0;

/* ★ WIFI 页面的控件 */
static lv_obj_t * scr_wifi = NULL;
static lv_obj_t * wifi_status_label = NULL;
static lv_obj_t * wifi_data_label = NULL;
static lv_obj_t * wifi_ip_label = NULL;

/* ★ WiFi 信号动画控件 */
static lv_obj_t * wifi_spinner = NULL;           // 加载动画
static lv_obj_t * wifi_dot = NULL;               // 中心点

/* ★ WIFI 接收缓冲区 */
#define WIFI_RX_BUF_SIZE 512
static char wifi_rx_buf[WIFI_RX_BUF_SIZE];
static volatile uint16_t wifi_rx_head = 0;
static volatile uint16_t wifi_rx_tail = 0;
static uint8_t wifi_rx_byte;

/* ★ WiFi 帧协议定义
 * 帧格式：
 * +--------+--------+------+------------------+--------+--------+
 * | 帧头H  | 帧头L  | 长度 |    数据 (N字节)   | 帧尾H  | 帧尾L  |
 * |  0xAA   |  0x55  |  N   |   ... payload    |  0x55  |  0xAA  |
 * +--------+--------+------+------------------+--------+--------+
 */
#define WIFI_FRAME_HEADER_H    0xAA
#define WIFI_FRAME_HEADER_L    0x55
#define WIFI_FRAME_TAIL_H      0x55
#define WIFI_FRAME_TAIL_L      0xAA
#define WIFI_FRAME_MAX_PAYLOAD 255

/* 帧解析状态机 */
typedef enum {
    WIFI_STATE_IDLE,        // 等待帧头高字节
    WIFI_STATE_HEADER_L,    // 等待帧头低字节
    WIFI_STATE_LENGTH,      // 等待长度字节
    WIFI_STATE_DATA,        // 接收数据
    WIFI_STATE_TAIL_H,      // 等待帧尾高字节
    WIFI_STATE_TAIL_L       // 等待帧尾低字节
} wifi_frame_state_t;

/* 帧解析上下文 */
static wifi_frame_state_t wifi_state = WIFI_STATE_IDLE;
static uint8_t wifi_frame_len = 0;           // 帧中声明的数据长度
static uint8_t wifi_frame_idx = 0;           // 当前接收到第几个数据字节
static uint8_t wifi_frame_buf[WIFI_FRAME_MAX_PAYLOAD]; // 帧数据缓冲区

/* 已解析完成的帧（供上层读取）- 使用队列避免丢失消息 */
#define WIFI_MSG_QUEUE_SIZE 4
static volatile uint8_t wifi_msg_count = 0;  /* 队列中的消息数量 */
static uint8_t wifi_msg_head = 0;  /* 队列头（写入位置） */
static uint8_t wifi_msg_tail = 0;  /* 队列尾（读取位置） */
static uint8_t wifi_msg_payload[WIFI_MSG_QUEUE_SIZE][WIFI_FRAME_MAX_PAYLOAD];
static uint8_t wifi_msg_len[WIFI_MSG_QUEUE_SIZE];
static volatile bool wifi_frame_ready = false;

/* ★ 按钮显示状态：false=隐藏，true=显示 */
static bool btn_visible = false;

/* ★ WiFi 连接状态 */
static bool wifi_connected = false;

/* ★ WiFi 调试标签 */
static lv_obj_t * wifi_debug_label = NULL;
static uint32_t wifi_rx_count = 0;  /* 接收字节计数 */
static uint32_t wifi_frame_count = 0;  /* 接收帧计数 */

/* ★ ADC 数据发送计数器（定时发送） */
static uint32_t wifi_adc_send_cnt = 0;
#define WIFI_ADC_SEND_INTERVAL 50  /* 每 50 个周期发送一次（100ms × 50 = 5 秒） */

/* ★ 主界面背景图片对象 */
static lv_obj_t * main_bg_img = NULL;

/* ★ UART 接收缓冲区 */
#define UART_RX_BUF_SIZE 256
static char uart_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t uart_rx_head = 0;
static volatile uint16_t uart_rx_tail = 0;
static uint8_t uart_rx_byte;

/* ================================================================
 *  WiFi 信号动画函数
 * ================================================================ */

/*
 * WiFi 信号动画：使用 LVGL 的 spinner 控件
 * 显示一个旋转的加载动画，表示 WiFi 连接状态
 */

/*
 * WiFi 发送帧函数
 * 将 payload 封装成协议帧格式发送给 ESP32-S3
 *
 * 帧格式：
 * +--------+--------+------+------------------+--------+--------+
 * | 0xAA   | 0x55  |  N   |   ... payload    | 0x55  | 0xAA  |
 * +--------+--------+------+------------------+--------+--------+
 *
 * 参数：
 *   payload - 要发送的数据
 *   len     - 数据长度（最大 255 字节）
 *
 * 返回：true=发送成功，false=参数错误
 */
static bool wifi_send_frame(const uint8_t * payload, uint8_t len)
{
    if(payload == NULL || len == 0 || len > WIFI_FRAME_MAX_PAYLOAD) {
        return false;
    }

    /* 构造帧：帧头 + 长度 + 数据 + 帧尾 */
    uint8_t frame[4 + WIFI_FRAME_MAX_PAYLOAD]; /* 最大帧长度 */
    uint16_t idx = 0;

    frame[idx++] = WIFI_FRAME_HEADER_H;  /* 帧头高 */
    frame[idx++] = WIFI_FRAME_HEADER_L;  /* 帧头低 */
    frame[idx++] = len;                  /* 长度 */

    /* 复制 payload */
    memcpy(&frame[idx], payload, len);
    idx += len;

    frame[idx++] = WIFI_FRAME_TAIL_H;    /* 帧尾高 */
    frame[idx++] = WIFI_FRAME_TAIL_L;    /* 帧尾低 */

    /* 通过 USART1 发送 */
    HAL_StatusTypeDef ret = HAL_UART_Transmit(&huart1, frame, idx, 100);
    return (ret == HAL_OK);
}

/*
 * 发送字符串的便捷函数
 */
static bool wifi_send_string(const char * str)
{
    if(str == NULL) return false;
    uint8_t len = strlen(str);
    if(len > WIFI_FRAME_MAX_PAYLOAD) len = WIFI_FRAME_MAX_PAYLOAD;
    return wifi_send_frame((const uint8_t *)str, len);
}

/* ================================================================
 *  UART 接收中断回调
 * ================================================================ */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == UART4) {
        uint16_t next_head = (uart_rx_head + 1) % UART_RX_BUF_SIZE;
        if(next_head != uart_rx_tail) {
            uart_rx_buf[uart_rx_head] = (char)uart_rx_byte;
            uart_rx_head = next_head;
            uart_rx_count++;
        }
        HAL_UART_Receive_IT(&huart4, &uart_rx_byte, 1);
    }
    else if(huart->Instance == USART1) {
        /* USART1 (ESP32S3 WiFi) 接收 - 帧协议解析 */
        uint8_t byte = wifi_rx_byte;

        switch(wifi_state) {
            case WIFI_STATE_IDLE:
                /* 等待帧头高字节 0xAA */
                if(byte == WIFI_FRAME_HEADER_H) {
                    wifi_state = WIFI_STATE_HEADER_L;
                }
                break;

            case WIFI_STATE_HEADER_L:
                /* 等待帧头低字节 0x55 */
                if(byte == WIFI_FRAME_HEADER_L) {
                    wifi_state = WIFI_STATE_LENGTH;
                } else if(byte == WIFI_FRAME_HEADER_H) {
                    /* 连续收到 0xAA，保持在当前状态 */
                    wifi_state = WIFI_STATE_HEADER_L;
                } else {
                    wifi_state = WIFI_STATE_IDLE;
                }
                break;

            case WIFI_STATE_LENGTH:
                /* 接收长度字节 */
                wifi_frame_len = byte;
                wifi_frame_idx = 0;
                if(wifi_frame_len == 0) {
                    /* 长度为0，直接等待帧尾 */
                    wifi_state = WIFI_STATE_TAIL_H;
                } else if(wifi_frame_len <= WIFI_FRAME_MAX_PAYLOAD) {
                    wifi_state = WIFI_STATE_DATA;
                } else {
                    /* 长度超限，丢弃 */
                    wifi_state = WIFI_STATE_IDLE;
                }
                break;

            case WIFI_STATE_DATA:
                /* 接收数据 */
                if(wifi_frame_idx < wifi_frame_len) {
                    wifi_frame_buf[wifi_frame_idx++] = byte;
                }
                if(wifi_frame_idx >= wifi_frame_len) {
                    wifi_state = WIFI_STATE_TAIL_H;
                }
                break;

            case WIFI_STATE_TAIL_H:
                /* 等待帧尾高字节 0x55 */
                if(byte == WIFI_FRAME_TAIL_H) {
                    wifi_state = WIFI_STATE_TAIL_L;
                } else {
                    /* 帧尾错误，丢弃 */
                    wifi_state = WIFI_STATE_IDLE;
                }
                break;

            case WIFI_STATE_TAIL_L:
                /* 等待帧尾低字节 0xAA */
                if(byte == WIFI_FRAME_TAIL_L) {
                    /* 帧接收完成！放入队列 */
                    uint8_t next_head = (wifi_msg_head + 1) % WIFI_MSG_QUEUE_SIZE;
                    if(next_head != wifi_msg_tail) {
                        memcpy(wifi_msg_payload[wifi_msg_head], wifi_frame_buf, wifi_frame_len);
                        wifi_msg_len[wifi_msg_head] = wifi_frame_len;
                        wifi_msg_payload[wifi_msg_head][wifi_frame_len] = '\0';
                        wifi_msg_head = next_head;
                        wifi_msg_count++;
                        wifi_frame_ready = true;
                    }
                    wifi_frame_count++;
                }
                /* 无论成功与否，都回到 IDLE */
                wifi_state = WIFI_STATE_IDLE;
                break;

            default:
                wifi_state = WIFI_STATE_IDLE;
                break;
        }

        /* 同时将原始字节存入环形缓冲区（用于调试和纯文本解析） */
        uint16_t next_head = (wifi_rx_head + 1) % WIFI_RX_BUF_SIZE;
        if(next_head != wifi_rx_tail) {
            wifi_rx_buf[wifi_rx_head] = (char)byte;
            wifi_rx_head = next_head;
        }
        wifi_rx_count++;

        /* ★ 纯文本模式：检测换行符作为消息结束 */
        if(byte == '\n' || byte == '\r') {
            /* 检查缓冲区中是否有完整的文本消息 */
            uint16_t len = 0;
            uint16_t tail = wifi_rx_tail;
            uint16_t head = wifi_rx_head;

            /* 计算消息长度 */
            if(head > tail) {
                len = head - tail;
            } else if(head < tail) {
                len = WIFI_RX_BUF_SIZE - tail + head;
            }

            if(len > 0 && len < WIFI_FRAME_MAX_PAYLOAD) {
                /* 将消息放入队列 */
                uint8_t next_head = (wifi_msg_head + 1) % WIFI_MSG_QUEUE_SIZE;
                if(next_head != wifi_msg_tail) {
                    /* 队列未满，保存消息 */
                    memcpy(wifi_msg_payload[wifi_msg_head], &wifi_rx_buf[tail], len);
                    wifi_msg_len[wifi_msg_head] = len;
                    wifi_msg_payload[wifi_msg_head][len] = '\0';
                    wifi_msg_head = next_head;
                    wifi_msg_count++;
                    wifi_frame_ready = true;
                }
                wifi_frame_count++;
                wifi_rx_tail = (tail + len) % WIFI_RX_BUF_SIZE;  /* 清空已处理的数据 */
            }
        }

        HAL_UART_Receive_IT(&huart1, &wifi_rx_byte, 1);
    }
}

/* ================================================================
 *  LVGL 事件回调函数（替代手动触摸检测）
 * ================================================================ */

/* 前向声明 */
static void dac_update_voltage(void);
static void pwm_update_duty(void);

/* 导航：跳转到指定页面（保留供将来使用） */
static void __attribute__((unused)) goto_page_cb(lv_event_t * e)
{
    lv_obj_t * target = (lv_obj_t *)lv_event_get_user_data(e);
    if(target) lv_screen_load_anim(target, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

/* 返回主界面 */
static void goto_main_cb(lv_event_t * e)
{
    (void)e;
    lv_screen_load_anim(scr_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

/* 小房子：切换其他按钮显示/隐藏 */
static void home_btn_cb(lv_event_t * e)
{
    (void)e;
    btn_visible = !btn_visible;

    /* 调试：显示切换状态 */
    if(debug_label) {
        lv_label_set_text_fmt(debug_label, "HOME: %s (btn_visible=%d)",
                             btn_visible ? "SHOW" : "HIDE", btn_visible);
    }

    /* 切换按钮显示/隐藏 */
    for(int i = 0; i < 8; i++) {
        if(btn[i]) {
            if(btn_visible) {
                lv_obj_clear_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/* DAC +0.1V */
static void dac_plus_cb(lv_event_t * e)
{
    (void)e;
    if(dac_voltage_mv <= 3200) dac_voltage_mv += 100;
    else dac_voltage_mv = 3300;
    dac_update_voltage();
}

/* DAC -0.1V */
static void dac_minus_cb(lv_event_t * e)
{
    (void)e;
    if(dac_voltage_mv >= 100) dac_voltage_mv -= 100;
    else dac_voltage_mv = 0;
    dac_update_voltage();
}

/* PWM +5% */
static void pwm_plus_cb(lv_event_t * e)
{
    (void)e;
    if(pwm_duty <= 95) pwm_duty += 5;
    else pwm_duty = 100;
    pwm_update_duty();
}

/* PWM -5% */
static void pwm_minus_cb(lv_event_t * e)
{
    (void)e;
    if(pwm_duty >= 5) pwm_duty -= 5;
    else pwm_duty = 0;
    pwm_update_duty();
}

/* TASK 开关状态变化 */
static void task_switch_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if(idx >= 0 && idx < 6) {
        task_enabled[idx] = lv_obj_has_state(sw, LV_STATE_CHECKED);
    }
}

/* WiFi 发送按钮回调 */
static void wifi_send_cb(lv_event_t * e)
{
    (void)e;
    /* 发送测试数据给 ESP32-S3 */
    const char * test_msg = "Hello ESP32";
    if(wifi_send_string(test_msg)) {
        if(wifi_data_label) {
            lv_label_set_text(wifi_data_label, "Sent: Hello ESP32");
        }
    } else {
        if(wifi_data_label) {
            lv_label_set_text(wifi_data_label, "Send Failed!");
        }
    }
}

/* WiFi 连接按钮回调 */
static void wifi_connect_cb(lv_event_t * e)
{
    (void)e;
    /* 发送连接命令给 ESP32-S3 */
    const char * cmd = "CONNECT";
    if(wifi_send_string(cmd)) {
        if(wifi_status_label) {
            lv_label_set_text(wifi_status_label, "Status: Connecting...");
            lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xFFFF00), 0);
        }
    }
}

/* ================================================================
 *  看门狗功能
 * ================================================================ */

/* 看门狗初始化函数 */
static void wdg_init(void)
{
    /* 配置 IWDG：超时时间约 1 秒
     * IWDG 时钟 = LSI / prescaler = 32kHz / 32 = 1kHz
     * 超时 = reload / 1kHz = 1000 / 1kHz = 1 秒
     */
    hiwdg.Instance = IWDG1;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 1000;
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        /* 初始化错误处理 */
        Error_Handler();
    }
}

/* 看门狗喂狗函数 */
static void wdg_refresh(void)
{
    if(wdg_enabled) {
        HAL_IWDG_Refresh(&hiwdg);
    }
}

/* 看门狗开关回调函数 */
static void wdg_switch_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if(checked) {
        /* 启用看门狗 */
        wdg_enabled = true;
        wdg_init();
        if(wdg_status_label) {
            lv_label_set_text(wdg_status_label, "Watchdog: ON");
            lv_obj_set_style_text_color(wdg_status_label, lv_color_hex(0x00FF00), 0);
        }
    } else {
        /* 禁用看门狗（IWDG 一旦启用无法真正禁用，只能停止喂狗） */
        wdg_enabled = false;
        if(wdg_status_label) {
            lv_label_set_text(wdg_status_label, "Watchdog: OFF");
            lv_obj_set_style_text_color(wdg_status_label, lv_color_hex(0xFF6B6B), 0);
        }
    }
}

/* SET 背景图片下拉框变化 */
static void bg_dropdown_cb(lv_event_t * e)
{
    lv_obj_t * dropdown = lv_event_get_target(e);
    uint16_t selected = lv_dropdown_get_selected(dropdown);

    /* 更新背景图片索引 */
    if(selected < BG_IMAGE_COUNT) {
        current_bg_index = selected;

        /* 更新预览图片 */
        if(set_preview_img) {
            lv_image_set_src(set_preview_img, bg_images[current_bg_index]);
        }

        /* 更新主界面背景图片 */
        if(main_bg_img) {
            lv_image_set_src(main_bg_img, bg_images[current_bg_index]);
        }
    }
}

/* ================================================================
 *  PWM 波形绘制函数（200×100 L8灰度画布）
 * ================================================================ */
static void pwm_draw_waveform(void)
{
    if(!pwm_canvas) return;

    lv_canvas_fill_bg(pwm_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(pwm_canvas, &layer);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0xFFFFFF);
    line_dsc.width = 2;

    int y_high = 10;
    int y_low  = PWM_CANVAS_H - 10;
    int period = PWM_CANVAS_W / 2;
    int high_px = period * pwm_duty / 100;
    if(high_px < 1) high_px = 1;
    if(high_px > period - 1) high_px = period - 1;

    for(int cycle = 0; cycle < 2; cycle++) {
        int x_start = cycle * period;

        line_dsc.p1.x = x_start;
        line_dsc.p1.y = y_high;
        line_dsc.p2.x = x_start + high_px;
        line_dsc.p2.y = y_high;
        lv_draw_line(&layer, &line_dsc);

        line_dsc.p1.x = x_start + high_px;
        line_dsc.p1.y = y_high;
        line_dsc.p2.x = x_start + high_px;
        line_dsc.p2.y = y_low;
        lv_draw_line(&layer, &line_dsc);

        line_dsc.p1.x = x_start + high_px;
        line_dsc.p1.y = y_low;
        line_dsc.p2.x = x_start + period;
        line_dsc.p2.y = y_low;
        lv_draw_line(&layer, &line_dsc);

        if(cycle < 1) {
            line_dsc.p1.x = x_start + period;
            line_dsc.p1.y = y_low;
            line_dsc.p2.x = x_start + period;
            line_dsc.p2.y = y_high;
            lv_draw_line(&layer, &line_dsc);
        }
    }

    lv_draw_line_dsc_t ref_dsc;
    lv_draw_line_dsc_init(&ref_dsc);
    ref_dsc.color = lv_color_hex(0x404040);
    ref_dsc.width = 1;
    ref_dsc.dash_width = 4;
    ref_dsc.dash_gap = 4;
    ref_dsc.p1.x = 0;
    ref_dsc.p1.y = PWM_CANVAS_H / 2;
    ref_dsc.p2.x = PWM_CANVAS_W - 1;
    ref_dsc.p2.y = PWM_CANVAS_H / 2;
    lv_draw_line(&layer, &ref_dsc);

    lv_canvas_finish_layer(pwm_canvas, &layer);
}

/* ================================================================
 *  DAC 波形绘制函数（200×100 L8灰度画布）
 * ================================================================ */
static void dac_draw_waveform(void)
{
    if(!dac_canvas) return;

    lv_canvas_fill_bg(dac_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(dac_canvas, &layer);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0xCCCCCC);
    line_dsc.width = 3;

    int y_margin = 10;
    int y_range = DAC_CANVAS_H - 2 * y_margin;
    int y = (DAC_CANVAS_H - y_margin) - (int)(dac_voltage_mv * y_range / 3300);
    if(y < y_margin) y = y_margin;
    if(y > DAC_CANVAS_H - y_margin) y = DAC_CANVAS_H - y_margin;

    line_dsc.p1.x = 0;
    line_dsc.p1.y = y;
    line_dsc.p2.x = DAC_CANVAS_W - 1;
    line_dsc.p2.y = y;
    lv_draw_line(&layer, &line_dsc);

    lv_draw_line_dsc_t ref_dsc;
    lv_draw_line_dsc_init(&ref_dsc);
    ref_dsc.color = lv_color_hex(0x404040);
    ref_dsc.width = 1;
    ref_dsc.dash_width = 4;
    ref_dsc.dash_gap = 4;
    ref_dsc.p1.x = 0;
    ref_dsc.p1.y = DAC_CANVAS_H / 2;
    ref_dsc.p2.x = DAC_CANVAS_W - 1;
    ref_dsc.p2.y = DAC_CANVAS_H / 2;
    lv_draw_line(&layer, &ref_dsc);

    ref_dsc.p1.y = DAC_CANVAS_H - y_margin;
    ref_dsc.p2.y = DAC_CANVAS_H - y_margin;
    ref_dsc.dash_width = 2;
    ref_dsc.dash_gap = 6;
    lv_draw_line(&layer, &ref_dsc);

    ref_dsc.p1.y = y_margin;
    ref_dsc.p2.y = y_margin;
    lv_draw_line(&layer, &ref_dsc);

    lv_canvas_finish_layer(dac_canvas, &layer);
}

/* ================================================================
 *  PWM 更新函数
 * ================================================================ */
static void pwm_update_duty(void)
{
    uint32_t compare = pwm_duty * 999 / 100;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, compare);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, compare);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, compare);

    pwm_draw_waveform();

    if(pwm_duty_label) {
        lv_label_set_text_fmt(pwm_duty_label,
                              "Duty: %lu%% | PA0 PA1 PB10 | 600Hz",
                              (unsigned long)pwm_duty);
    }
}

/* ================================================================
 *  DAC 更新函数
 * ================================================================ */
static void dac_update_voltage(void)
{
    dac_draw_waveform();

    if(dac_voltage_label) {
        uint32_t int_part = dac_voltage_mv / 1000;
        uint32_t dec_part = dac_voltage_mv % 1000;
        lv_label_set_text_fmt(dac_voltage_label,
                              "Voltage: %lu.%03luV | PA4/PA5=SPI",
                              (unsigned long)int_part,
                              (unsigned long)dec_part);
    }
}

/* ================================================================
 *  定时器回调（只用 raw 坐标检测触摸）
 * ================================================================ */
/*
 * 按钮索引：   0=ADC  1=DAC  2=PWM
 *              3=UART 4=CAM  5=WIFI
 *              6=TASK 7=SET  8=HOME
 *
 * raw 坐标范围：
 *   行0 (ADC,DAC,PWM):     raw_x 30-70
 *   行1 (UART,CAM,WIFI):   raw_x 90-130
 *   行2 (TASK,SET,HOME):   raw_x 150-190
 *   列0 (ADC,90,TASK):     raw_y 30-90
 *   列1 (DAC,CAM,SET):     raw_y 130-190
 *   列2 (PWM,WIFI,HOME):   raw_y 230-290
 */
static void data_update_timer_cb(lv_timer_t * timer)
{
    (void)timer;

    /* 喂狗（如果看门狗已启用） */
    wdg_refresh();

    /* 只读 raw 坐标 */
    int32_t raw_x, raw_y;
    bool pressed;
    lv_port_indev_get_raw(&raw_x, &raw_y, &pressed);

    /* 更新调试标签：显示 raw 坐标和btn_visible状态 */
    if(debug_label && pressed) {
        lv_label_set_text_fmt(debug_label, "raw:%ld,%ld bv%d", raw_x, raw_y, btn_visible);
    }

    /* 用 raw 坐标检测触摸 */
    static bool was_pressed = false;
    if(pressed) {
        if(!was_pressed) {
            was_pressed = true;

            /* 主界面：检测按钮 */
            if(lv_screen_active() == scr_main) {
                /* HOME 按钮：下面一行，右边 raw_x:30-70, raw_y:230-290 */
                if(raw_x >= 30 && raw_x <= 70 && raw_y >= 230 && raw_y <= 290) {
                    home_btn_cb(NULL);
                }
                /* 其他按钮：只在显示状态下才能点击 */
                else if(btn_visible) {
                    /* ADC: 上面一行，左边 raw_x:150-190, raw_y:30-90 */
                    if(raw_x >= 150 && raw_x <= 190 && raw_y >= 30 && raw_y <= 90) {
                        if(scr_adc) lv_screen_load_anim(scr_adc, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    }
                    /* DAC: 上面一行，中间 raw_x:150-190, raw_y:130-190 */
                    else if(raw_x >= 150 && raw_x <= 190 && raw_y >= 130 && raw_y <= 190) {
                        if(scr_dac) lv_screen_load_anim(scr_dac, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    }
                    /* PWM: 上面一行，右边 raw_x:150-190, raw_y:230-290 */
                    else if(raw_x >= 150 && raw_x <= 190 && raw_y >= 230 && raw_y <= 290) {
                        if(scr_pwm) lv_screen_load_anim(scr_pwm, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    }
                    /* UART: 中间一行，左边 raw_x:90-130, raw_y:30-90 */
                    else if(raw_x >= 90 && raw_x <= 130 && raw_y >= 30 && raw_y <= 90) {
                        if(scr_uart) lv_screen_load_anim(scr_uart, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    }
                    /* TASK: 下面一行，左边 raw_x:30-70, raw_y:30-90 */
                    else if(raw_x >= 30 && raw_x <= 70 && raw_y >= 30 && raw_y <= 90) {
                        if(scr_task) lv_screen_load_anim(scr_task, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    }
                    /* SET: 下面一行，中间 raw_x:30-70, raw_y:130-190 */
                    else if(raw_x >= 30 && raw_x <= 70 && raw_y >= 130 && raw_y <= 190) {
                        if(scr_set) lv_screen_load_anim(scr_set, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    }
                    /* WIFI: 中间一行，右边 raw_x:90-130, raw_y:230-290 */
                    else if(raw_x >= 90 && raw_x <= 130 && raw_y >= 230 && raw_y <= 290) {
                        if(scr_wifi) lv_screen_load_anim(scr_wifi, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    }
                }
            }
            /* 子页面：检测 Back 按钮 */
            else if(lv_screen_active() != scr_main) {
                /* Back 按钮 raw 范围：左下角 */
                if(raw_x >= 200 && raw_x <= 239 && raw_y >= 0 && raw_y <= 50) {
                    goto_main_cb(NULL);
                }
                /* DAC/PWM +/- 按钮 */
                else if(lv_screen_active() == scr_dac || lv_screen_active() == scr_pwm) {
                    /* + 按钮 raw 范围：右上角 */
                    if(raw_x >= 0 && raw_x <= 30 && raw_y >= 0 && raw_y <= 50) {
                        if(lv_screen_active() == scr_dac) dac_plus_cb(NULL);
                        else pwm_plus_cb(NULL);
                    }
                    /* - 按钮 raw 范围：右侧中间 */
                    else if(raw_x >= 0 && raw_x <= 30 && raw_y >= 80 && raw_y <= 130) {
                        if(lv_screen_active() == scr_dac) dac_minus_cb(NULL);
                        else pwm_minus_cb(NULL);
                    }
                }
            }
        }
    } else {
        was_pressed = false;
    }

    /* ADC 页面：更新 5 个通道的数值显示 */
    if(lv_screen_active() == scr_adc && adc_labels[0]) {
        const char * ch_names[5] = {
            "CH2(PF11)", "CH10(PC0)", "CH11(PC1)", "CH14(PA2)", "CH15(PA3)"
        };
        for(int i = 0; i < 5; i++) {
            char buf[32];
            uint32_t raw = g_adc_raw[i];
            uint32_t mv = raw * 3300 / 65535;
            uint32_t int_part = mv / 1000;
            uint32_t dec_part = mv % 1000;
            snprintf(buf, sizeof(buf), "%s: %5lu  %lu.%03luV",
                     ch_names[i], (unsigned long)raw,
                     (unsigned long)int_part, (unsigned long)dec_part);
            lv_label_set_text(adc_labels[i], buf);
        }
    }

    /* UART 页面：更新接收数据显示 */
    if(lv_screen_active() == scr_uart) {
        uint16_t tail = uart_rx_tail;
        uint16_t head = uart_rx_head;
        if(tail != head && uart_rx_label) {
            uint16_t disp_len = strlen(uart_disp_buf);
            while(tail != head && disp_len < UART_DISP_BUF_SIZE - 2) {
                char c = uart_rx_buf[tail];
                if(c >= 32 && c < 127) {
                    uart_disp_buf[disp_len++] = c;
                } else if(c == '\n' || c == '\r') {
                    uart_disp_buf[disp_len++] = '\n';
                } else {
                    uart_disp_buf[disp_len++] = '.';
                }
                tail = (tail + 1) % UART_RX_BUF_SIZE;
            }
            uart_disp_buf[disp_len] = '\0';
            uart_rx_tail = tail;

            if(disp_len > UART_DISP_BUF_SIZE - 32) {
                uint16_t keep = UART_DISP_BUF_SIZE / 2;
                memmove(uart_disp_buf, uart_disp_buf + disp_len - keep, keep + 1);
            }

            lv_label_set_text(uart_rx_label, uart_disp_buf);
            lv_obj_scroll_to_y(lv_obj_get_parent(uart_rx_label), LV_COORD_MAX, LV_ANIM_ON);
        }
        if(uart_info_label) {
            lv_label_set_text_fmt(uart_info_label,
                "UART4  115200bps  PA11=RX  PA12=TX  RX:%lu",
                (unsigned long)uart_rx_count);
        }
    }

    /* WIFI 页面：更新接收数据显示（使用队列中的数据） */
    while(wifi_msg_count > 0) {
        /* 从队列中读取消息 */
        uint8_t tail = wifi_msg_tail;
        char payload_str[256];
        uint8_t payload_len = wifi_msg_len[tail];
        if(payload_len >= sizeof(payload_str)) {
            payload_len = sizeof(payload_str) - 1;
        }
        memcpy(payload_str, wifi_msg_payload[tail], payload_len);
        payload_str[payload_len] = '\0';

        /* 移动队列尾指针 */
        wifi_msg_tail = (tail + 1) % WIFI_MSG_QUEUE_SIZE;
        wifi_msg_count--;

        /* 检查队列是否为空 */
        if(wifi_msg_count == 0) {
            wifi_frame_ready = false;
        }

        /* 调试：显示收到的所有数据 */
        if(wifi_data_label) {
            char debug_buf[256];
            snprintf(debug_buf, sizeof(debug_buf), "[%u] Q:%u RX:%u FR:%u %s",
                     payload_len, (unsigned)wifi_msg_count, (unsigned)wifi_rx_count, (unsigned)wifi_frame_count, payload_str);
            lv_label_set_text(wifi_data_label, debug_buf);
        }

        /* 检查是否是 "connect" 消息（ESP32-S3 热点有设备连接） */
        if(strstr(payload_str, "connect") != NULL) {
            /* 收到 connect 消息，更新 WiFi 状态 */
            wifi_connected = true;
            wifi_adc_send_cnt = 0;  /* 重置发送计数器 */
            if(wifi_status_label) {
                lv_label_set_text(wifi_status_label, "Status: WiFi Connected");
                lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0x00FF00), 0);
            }
        }
        /* 检查是否是 "discon" 消息（WiFi 断开连接） */
        else if(strstr(payload_str, "discon") != NULL) {
            wifi_connected = false;
            if(wifi_status_label) {
                lv_label_set_text(wifi_status_label, "Status: Disconnected");
                lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xFF6B6B), 0);
            }
            if(wifi_ip_label) {
                lv_label_set_text(wifi_ip_label, "IP: ---.---.---.---");
            }
            if(wifi_data_label) {
                lv_label_set_text(wifi_data_label, "Device disconnected");
            }
        }
        /* 检查是否包含 IP 地址（格式：http://xxx.xxx.xxx.xxx 或 IP:xxx.xxx.xxx.xxx 或纯IP地址） */
        else if(strstr(payload_str, "http://") != NULL || strstr(payload_str, "IP:") != NULL ||
                strstr(payload_str, "192.168.") != NULL || strstr(payload_str, "10.0.") != NULL) {
            /* 收到 IP 地址，更新显示 */
            if(wifi_ip_label) {
                /* 提取 IP 部分 */
                char * ip_start = strstr(payload_str, "http://");
                if(ip_start == NULL) ip_start = strstr(payload_str, "IP:");
                if(ip_start == NULL) ip_start = strstr(payload_str, "192.168.");
                if(ip_start == NULL) ip_start = strstr(payload_str, "10.0.");
                if(ip_start != NULL) {
                    lv_label_set_text(wifi_ip_label, ip_start);
                }
            }
        }
        /* 其他消息，正常显示 */
        else if(lv_screen_active() == scr_wifi && wifi_data_label) {
            /* 显示格式：[长度] RX:计数 FR:计数 内容 */
            char wifi_disp_buf[256];
            snprintf(wifi_disp_buf, sizeof(wifi_disp_buf), "[%u] RX:%u FR:%u %s",
                     payload_len, (unsigned)wifi_rx_count, (unsigned)wifi_frame_count, payload_str);

            lv_label_set_text(wifi_data_label, wifi_disp_buf);
            lv_obj_scroll_to_y(lv_obj_get_parent(wifi_data_label), LV_COORD_MAX, LV_ANIM_ON);
        }
    }  /* end while */

    /* ★ 定时发送 ADC 数据到 ESP32-S3（每 5 秒一次） */
    if(wifi_connected) {
        wifi_adc_send_cnt++;
        if(wifi_adc_send_cnt >= WIFI_ADC_SEND_INTERVAL) {
            wifi_adc_send_cnt = 0;

            /* 构造 ADC 数据字符串
             * 格式：ADC:CH0=1234,CH1=2345,CH2=3456,CH3=4567,CH4=5678
             */
            char adc_buf[128];
            int len = snprintf(adc_buf, sizeof(adc_buf),
                               "ADC:%lu,%lu,%lu,%lu,%lu",
                               (unsigned long)g_adc_raw[0],
                               (unsigned long)g_adc_raw[1],
                               (unsigned long)g_adc_raw[2],
                               (unsigned long)g_adc_raw[3],
                               (unsigned long)g_adc_raw[4]);

            if(len > 0 && len < sizeof(adc_buf)) {
                wifi_send_string(adc_buf);
            }
        }
    }

    /* ★ WiFi 调试信息更新（每 500ms 更新一次） */
    if(wifi_debug_label && lv_screen_active() == scr_wifi) {
        static uint32_t dbg_cnt = 0;
        dbg_cnt++;
        if(dbg_cnt >= 5) {  /* 100ms × 5 = 500ms */
            dbg_cnt = 0;
            lv_label_set_text_fmt(wifi_debug_label,
                                  "RX:%lu FR:%lu ST:%d",
                                  (unsigned long)wifi_rx_count,
                                  (unsigned long)wifi_frame_count,
                                  (int)wifi_state);
        }
    }
}

/* ================================================================
 *  创建各页面的函数（全部使用绝对定位）
 * ================================================================ */

/*
 * 屏幕尺寸：320×240
 * 坐标系：x=0~320（左→右），y=0~240（上→下）
 *
 * 主界面布局（9个按钮，80×40，3列×3行）：
 * ┌────────────────────────────────────┐
 * │ y=40  [ADC]     [DAC]     [PWM]   │
 * │ y=100 [UART]    [CAM]     [WIFI]  │
 * │ y=160 [TASK]    [SET]     [HOME]  │
 * └────────────────────────────────────┘
 *   x=20    x=120      x=220
 */
/*
 * 主界面布局（raw 坐标系）
 *
 * 屏幕：raw_x 0-240（垂直），raw_y 0-320（水平）
 *
 * 9 个按钮，3列×3行
 * 按钮大小：60×40（raw 坐标）
 *
 * ┌─────────────────────────────────────────────────┐
 * │                                                 │
 * │  [TASK]           [SET]            [HOME]       │
 * │  raw_x:150-190    raw_x:150-190    raw_x:150-190│
 * │  raw_y:30-90      raw_y:130-190    raw_y:230-290│
 * │                                                 │
 * │  [UART]           [CAM]            [WIFI]       │
 * │  raw_x:90-130     raw_x:90-130     raw_x:90-130 │
 * │  raw_y:30-90      raw_y:130-190    raw_y:230-290│
 * │                                                 │
 * │  [ADC]            [DAC]            [PWM]        │
 * │  raw_x:30-70      raw_x:30-70      raw_x:30-70 │
 * │  raw_y:30-90      raw_y:130-190    raw_y:230-290│
 * │                                                 │
 * └─────────────────────────────────────────────────┘
 */

static void create_main_screen(void)
{
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, lv_color_hex(0x101828), 0);
    lv_obj_set_style_bg_opa(scr_main, LV_OPA_COVER, 0);

    /* 背景图片 */
    main_bg_img = lv_image_create(scr_main);
    lv_image_set_src(main_bg_img, &photo_01);
    lv_obj_set_size(main_bg_img, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(main_bg_img, 0, 0);

    /* 按钮文字 */
    static const char * btn_text[9] = {
        "ADC", "DAC", "PWM", "UART", "CAM", "WIFI", "TASK", "SET", NULL
    };

    /*
     * 按钮显示位置（LVGL 坐标）
     * 由于屏幕旋转，LVGL 坐标和 raw 坐标不同
     * 这里先用固定位置显示，触摸检测用 raw 坐标
     */
    static const int16_t btn_lvgl_x[9] = { 20,  120, 220,   20, 120, 220,   20, 120, 220 };
    static const int16_t btn_lvgl_y[9] = { 40,   40,  40,  100, 100, 100,  160, 160, 160 };

    for(int i = 0; i < 9; i++) {
        btn[i] = lv_button_create(scr_main);
        lv_obj_set_size(btn[i], 80, 40);
        lv_obj_set_pos(btn[i], btn_lvgl_x[i], btn_lvgl_y[i]);
        lv_obj_set_style_bg_color(btn[i], lv_color_hex(0x3498DB), 0);
        lv_obj_set_style_bg_opa(btn[i], LV_OPA_50, 0);

        lv_obj_t * label = lv_label_create(btn[i]);
        if(i == 8) lv_label_set_text(label, LV_SYMBOL_HOME);
        else       lv_label_set_text(label, btn_text[i]);
        lv_obj_center(label);

        /* 初始状态：前8个隐藏，小房子显示 */
        if(i < 8) lv_obj_add_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
    }
    btn_visible = false;

    /* 调试标签：显示 raw 坐标 */
    debug_label = lv_label_create(scr_main);
    lv_label_set_text(debug_label, "raw:---,---");
    lv_obj_set_style_text_color(debug_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(debug_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(debug_label, 5, 5);
}

/*
 * 子页面模板（使用绝对定位，320×240 横屏）
 * 标题：y=8，水平居中
 * Back 按钮：左下角 (10, 205)，80×35
 */
static void create_sub_page_template(lv_obj_t ** page_ptr, uint32_t bg_color, const char * title_text)
{
    *page_ptr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(*page_ptr, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(*page_ptr, LV_OPA_COVER, 0);

    /* 背景图片：320×240 */
    lv_obj_t * bg = lv_image_create(*page_ptr);
    lv_image_set_src(bg, &photo_01);
    lv_obj_set_size(bg, 320, 240);
    lv_obj_set_pos(bg, 0, 0);

    /* 标题：顶部居中 y=8 */
    lv_obj_t * title = lv_label_create(*page_ptr);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* Back 按钮：左下角 (10, 205)，80×35 */
    lv_obj_t * btn_back = lv_button_create(*page_ptr);
    lv_obj_set_size(btn_back, 80, 35);
    lv_obj_set_pos(btn_back, 10, 205);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xE74C3C), 0);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, goto_main_cb, LV_EVENT_CLICKED, NULL);
}

/* ================================================================
 *  入口函数
 * ================================================================ */
void ui_init(void)
{
    /* 第1步：创建所有页面 */
    create_main_screen();

    /* ADC 页面 */
    create_sub_page_template(&scr_adc, 0x2C3E50, "ADC Monitor");
    {
        const char * ch_names[5] = {
            "CH2(PF11)", "CH10(PC0)", "CH11(PC1)", "CH14(PA2)", "CH15(PA3)"
        };
        for(int i = 0; i < 5; i++) {
            adc_labels[i] = lv_label_create(scr_adc);
            lv_label_set_text_fmt(adc_labels[i], "%s: -----  -.--V", ch_names[i]);
            lv_obj_set_style_text_color(adc_labels[i], lv_color_hex(0x00FF00), 0);
            lv_obj_set_style_text_font(adc_labels[i], &lv_font_montserrat_14, 0);
            lv_obj_set_pos(adc_labels[i], 30, 35 + i * 22);  /* 绝对坐标 */
        }
    }

    /* DAC 页面 */
    create_sub_page_template(&scr_dac, 0x2C3E50, "DAC Output");
    {
        /* 画布：绝对定位 (60, 30)，200×100 */
        dac_canvas = lv_canvas_create(scr_dac);
        lv_canvas_set_buffer(dac_canvas, dac_canvas_buf,
                             DAC_CANVAS_W, DAC_CANVAS_H,
                             LV_COLOR_FORMAT_L8);
        lv_obj_set_pos(dac_canvas, 60, 30);  /* 绝对坐标 */
        lv_obj_set_style_border_color(dac_canvas, lv_color_hex(0x555555), 0);
        lv_obj_set_style_border_width(dac_canvas, 1, 0);
        lv_obj_set_style_border_opa(dac_canvas, LV_OPA_COVER, 0);

        /* +0.1V 按钮：绝对定位 (275, 35)，45×35 */
        lv_obj_t * btn_plus = lv_button_create(scr_dac);
        lv_obj_set_size(btn_plus, 45, 35);
        lv_obj_set_pos(btn_plus, 275, 35);  /* 绝对坐标 */
        lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x27AE60), 0);
        lv_obj_t * lbl_plus = lv_label_create(btn_plus);
        lv_label_set_text(lbl_plus, "+0.1V");
        lv_obj_center(lbl_plus);
        lv_obj_add_event_cb(btn_plus, dac_plus_cb, LV_EVENT_CLICKED, NULL);

        /* -0.1V 按钮：绝对定位 (275, 85)，45×35 */
        lv_obj_t * btn_minus = lv_button_create(scr_dac);
        lv_obj_set_size(btn_minus, 45, 35);
        lv_obj_set_pos(btn_minus, 275, 85);  /* 绝对坐标 */
        lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0xE67E22), 0);
        lv_obj_t * lbl_minus = lv_label_create(btn_minus);
        lv_label_set_text(lbl_minus, "-0.1V");
        lv_obj_center(lbl_minus);
        lv_obj_add_event_cb(btn_minus, dac_minus_cb, LV_EVENT_CLICKED, NULL);

        /* 电压标签：绝对定位 (40, 138) */
        dac_voltage_label = lv_label_create(scr_dac);
        lv_label_set_text(dac_voltage_label, "Voltage: 1.650V  |  PA4/PA5=SPI");
        lv_obj_set_style_text_color(dac_voltage_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(dac_voltage_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(dac_voltage_label, 40, 138);  /* 绝对坐标 */

        dac_draw_waveform();
    }

    /* PWM 页面 */
    create_sub_page_template(&scr_pwm, 0x2C3E50, "PWM Output");
    {
        /* 画布：绝对定位 (60, 30)，200×100 */
        pwm_canvas = lv_canvas_create(scr_pwm);
        lv_canvas_set_buffer(pwm_canvas, pwm_canvas_buf,
                             PWM_CANVAS_W, PWM_CANVAS_H,
                             LV_COLOR_FORMAT_L8);
        lv_obj_set_pos(pwm_canvas, 60, 30);  /* 绝对坐标 */
        lv_obj_set_style_border_color(pwm_canvas, lv_color_hex(0x555555), 0);
        lv_obj_set_style_border_width(pwm_canvas, 1, 0);
        lv_obj_set_style_border_opa(pwm_canvas, LV_OPA_COVER, 0);

        /* +5% 按钮：绝对定位 (275, 35)，45×35 */
        lv_obj_t * btn_plus = lv_button_create(scr_pwm);
        lv_obj_set_size(btn_plus, 45, 35);
        lv_obj_set_pos(btn_plus, 275, 35);  /* 绝对坐标 */
        lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x27AE60), 0);
        lv_obj_t * lbl_plus = lv_label_create(btn_plus);
        lv_label_set_text(lbl_plus, "+5%");
        lv_obj_center(lbl_plus);
        lv_obj_add_event_cb(btn_plus, pwm_plus_cb, LV_EVENT_CLICKED, NULL);

        /* -5% 按钮：绝对定位 (275, 85)，45×35 */
        lv_obj_t * btn_minus = lv_button_create(scr_pwm);
        lv_obj_set_size(btn_minus, 45, 35);
        lv_obj_set_pos(btn_minus, 275, 85);  /* 绝对坐标 */
        lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0xE67E22), 0);
        lv_obj_t * lbl_minus = lv_label_create(btn_minus);
        lv_label_set_text(lbl_minus, "-5%");
        lv_obj_center(lbl_minus);
        lv_obj_add_event_cb(btn_minus, pwm_minus_cb, LV_EVENT_CLICKED, NULL);

        /* 占空比标签：绝对定位 (40, 138) */
        pwm_duty_label = lv_label_create(scr_pwm);
        lv_label_set_text(pwm_duty_label, "Duty: 50%  |  PA0 PA1 PB10  |  600Hz");
        lv_obj_set_style_text_color(pwm_duty_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(pwm_duty_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(pwm_duty_label, 40, 138);  /* 绝对坐标 */

        pwm_draw_waveform();
    }

    /* UART 页面 */
    create_sub_page_template(&scr_uart, 0x2C3E50, "UART Terminal");
    {
        /* 状态信息：绝对定位 (15, 45) */
        uart_info_label = lv_label_create(scr_uart);
        lv_label_set_text(uart_info_label, "UART4  115200bps  PA11=RX  PA12=TX  RX:0");
        lv_obj_set_style_text_color(uart_info_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(uart_info_label, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(uart_info_label, 15, 45);  /* 绝对坐标 */

        /* 接收显示区容器：绝对定位 (15, 65)，290×130 */
        lv_obj_t * rx_cont = lv_obj_create(scr_uart);
        lv_obj_set_size(rx_cont, 290, 130);
        lv_obj_set_pos(rx_cont, 15, 65);  /* 绝对坐标 */
        lv_obj_set_style_bg_color(rx_cont, lv_color_hex(0x0A0A0A), 0);
        lv_obj_set_style_bg_opa(rx_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(rx_cont, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_border_width(rx_cont, 1, 0);
        lv_obj_set_style_border_opa(rx_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(rx_cont, 5, 0);
        lv_obj_set_style_radius(rx_cont, 4, 0);
        lv_obj_set_scroll_dir(rx_cont, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(rx_cont, LV_SCROLLBAR_MODE_AUTO);

        /* 接收数据标签：在容器内，绝对定位 (0, 0) */
        uart_rx_label = lv_label_create(rx_cont);
        lv_label_set_text(uart_rx_label, "Waiting for data...");
        lv_obj_set_style_text_color(uart_rx_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(uart_rx_label, &lv_font_montserrat_14, 0);
        lv_obj_set_width(uart_rx_label, 270);
        lv_label_set_long_mode(uart_rx_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(uart_rx_label, 0, 0);  /* 绝对坐标 */
    }

    /* TASK 页面 */
    create_sub_page_template(&scr_task, 0x2C3E50, "TASK");
    {
        const char * task_names[6] = {"ADC", "DAC", "PWM", "UART", "CAM", "WIFI"};
        for(int i = 0; i < 6; i++) {
            int row = i / 2;
            int col = i % 2;
            int x_label = (col == 0) ? 40 : 170;
            int x_switch = (col == 0) ? 100 : 230;
            int y_pos = 45 + row * 45;

            /* 标签：绝对坐标 */
            lv_obj_t * label = lv_label_create(scr_task);
            lv_label_set_text(label, task_names[i]);
            lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(label, x_label, y_pos);  /* 绝对坐标 */

            /* 开关：绝对坐标 */
            task_switches[i] = lv_switch_create(scr_task);
            lv_obj_set_size(task_switches[i], 50, 25);
            lv_obj_set_pos(task_switches[i], x_switch, y_pos - 3);  /* 绝对坐标 */
            lv_obj_set_style_bg_color(task_switches[i], lv_color_hex(0x27AE60), LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_remove_state(task_switches[i], LV_STATE_CHECKED);
            lv_obj_add_event_cb(task_switches[i], task_switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        }
    }

    /* WIFI 页面 */
    create_sub_page_template(&scr_wifi, 0x2C3E50, "WiFi - ESP32S3");
    {
        /* ★ WiFi 信号动画：使用 spinner 控件
         * 显示一个旋转的加载动画，表示 WiFi 连接状态
         * 位置：页面顶部居中
         */
        wifi_spinner = lv_spinner_create(scr_wifi);
        lv_obj_set_size(wifi_spinner, 60, 60);
        lv_obj_set_pos(wifi_spinner, 130, 20);

        /* 设置 spinner 样式 */
        lv_obj_set_style_arc_color(wifi_spinner, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_arc_width(wifi_spinner, 4, 0);
        lv_obj_set_style_arc_rounded(wifi_spinner, true, 0);

        /* 设置旋转速度：1 秒一圈 */
        lv_spinner_set_anim_params(wifi_spinner, 1000, 200);

        /* 中心点：WiFi 源 */
        wifi_dot = lv_obj_create(scr_wifi);
        lv_obj_set_size(wifi_dot, 10, 10);
        lv_obj_set_pos(wifi_dot, 155, 45);
        lv_obj_set_style_bg_color(wifi_dot, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_radius(wifi_dot, 5, 0);
        lv_obj_set_style_border_width(wifi_dot, 0, 0);

        /* 连接状态：绝对定位 (15, 85) */
        wifi_status_label = lv_label_create(scr_wifi);
        lv_label_set_text(wifi_status_label, "Status: Disconnected");
        lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xFF6B6B), 0);
        lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(wifi_status_label, 15, 85);

        /* IP 地址：绝对定位 (15, 105) */
        wifi_ip_label = lv_label_create(scr_wifi);
        lv_label_set_text(wifi_ip_label, "IP: ---.---.---.---");
        lv_obj_set_style_text_color(wifi_ip_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(wifi_ip_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(wifi_ip_label, 15, 105);

        /* 数据接收区容器：绝对定位 (15, 125)，290×50 */
        lv_obj_t * rx_cont = lv_obj_create(scr_wifi);
        lv_obj_set_size(rx_cont, 290, 50);
        lv_obj_set_pos(rx_cont, 15, 125);
        lv_obj_set_style_bg_color(rx_cont, lv_color_hex(0x0A0A0A), 0);
        lv_obj_set_style_bg_opa(rx_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(rx_cont, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_border_width(rx_cont, 1, 0);
        lv_obj_set_style_border_opa(rx_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(rx_cont, 5, 0);
        lv_obj_set_style_radius(rx_cont, 4, 0);
        lv_obj_set_scroll_dir(rx_cont, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(rx_cont, LV_SCROLLBAR_MODE_AUTO);

        /* 数据接收标签：在容器内 */
        wifi_data_label = lv_label_create(rx_cont);
        lv_label_set_text(wifi_data_label, "Waiting for data...");
        lv_obj_set_style_text_color(wifi_data_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(wifi_data_label, &lv_font_montserrat_14, 0);
        lv_obj_set_width(wifi_data_label, 270);
        lv_label_set_long_mode(wifi_data_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(wifi_data_label, 0, 0);

        /* 发送按钮：绝对定位 (15, 180) */
        lv_obj_t * btn_send = lv_button_create(scr_wifi);
        lv_obj_set_size(btn_send, 80, 25);
        lv_obj_set_pos(btn_send, 15, 180);
        lv_obj_set_style_bg_color(btn_send, lv_color_hex(0x27AE60), 0);
        lv_obj_t * lbl_send = lv_label_create(btn_send);
        lv_label_set_text(lbl_send, "Send");
        lv_obj_center(lbl_send);
        lv_obj_add_event_cb(btn_send, wifi_send_cb, LV_EVENT_CLICKED, NULL);

        /* 连接按钮：绝对定位 (110, 180) */
        lv_obj_t * btn_connect = lv_button_create(scr_wifi);
        lv_obj_set_size(btn_connect, 80, 25);
        lv_obj_set_pos(btn_connect, 110, 180);
        lv_obj_set_style_bg_color(btn_connect, lv_color_hex(0x3498DB), 0);
        lv_obj_t * lbl_connect = lv_label_create(btn_connect);
        lv_label_set_text(lbl_connect, "Connect");
        lv_obj_center(lbl_connect);
        lv_obj_add_event_cb(btn_connect, wifi_connect_cb, LV_EVENT_CLICKED, NULL);

        /* 调试标签：显示接收统计（右下角） */
        wifi_debug_label = lv_label_create(scr_wifi);
        lv_label_set_text(wifi_debug_label, "RX:0 FR:0 ST:0");
        lv_obj_set_style_text_color(wifi_debug_label, lv_color_hex(0xFFFF00), 0);
        lv_obj_set_style_text_font(wifi_debug_label, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(wifi_debug_label, 200, 210);
    }

    /* SET 页面 */
    create_sub_page_template(&scr_set, 0x2C3E50, "Settings");
    {
        /* 背景图片选择标签 */
        lv_obj_t * bg_label = lv_label_create(scr_set);
        lv_label_set_text(bg_label, "Background Image:");
        lv_obj_set_style_text_color(bg_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(bg_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(bg_label, 15, 35);

        /* 下拉框：选择背景图片 */
        set_dropdown = lv_dropdown_create(scr_set);
        lv_dropdown_set_options(set_dropdown, "Image 01\nImage 02\nImage 03");
        lv_obj_set_size(set_dropdown, 150, 35);
        lv_obj_set_pos(set_dropdown, 15, 55);
        lv_obj_set_style_bg_color(set_dropdown, lv_color_hex(0x3498DB), 0);
        lv_obj_set_style_text_color(set_dropdown, lv_color_hex(0xFFFFFF), 0);
        lv_obj_add_event_cb(set_dropdown, bg_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

        /* 预览图片：显示当前选中的背景 */
        set_preview_img = lv_image_create(scr_set);
        lv_image_set_src(set_preview_img, bg_images[current_bg_index]);
        lv_obj_set_size(set_preview_img, 160, 120);
        lv_obj_set_pos(set_preview_img, 140, 35);
        lv_obj_set_style_border_color(set_preview_img, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(set_preview_img, 2, 0);

        /* 提示标签 */
        lv_obj_t * hint_label = lv_label_create(scr_set);
        lv_label_set_text(hint_label, "Select image to change background");
        lv_obj_set_style_text_color(hint_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(hint_label, 15, 100);

        /* ================================================================
         *  看门狗功能区域
         * ================================================================ */

        /* 看门狗标签 */
        lv_obj_t * wdg_label = lv_label_create(scr_set);
        lv_label_set_text(wdg_label, "Watchdog Timer:");
        lv_obj_set_style_text_color(wdg_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(wdg_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(wdg_label, 15, 130);

        /* 看门狗开关 */
        wdg_switch = lv_switch_create(scr_set);
        lv_obj_set_size(wdg_switch, 50, 25);
        lv_obj_set_pos(wdg_switch, 15, 150);
        lv_obj_set_style_bg_color(wdg_switch, lv_color_hex(0x95A5A6), 0);  /* 关闭状态：灰色 */
        lv_obj_set_style_bg_color(wdg_switch, lv_color_hex(0x27AE60), LV_STATE_CHECKED);  /* 开启状态：绿色 */
        lv_obj_add_event_cb(wdg_switch, wdg_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

        /* 看门狗状态标签 */
        wdg_status_label = lv_label_create(scr_set);
        lv_label_set_text(wdg_status_label, "Watchdog: OFF");
        lv_obj_set_style_text_color(wdg_status_label, lv_color_hex(0xFF6B6B), 0);
        lv_obj_set_style_text_font(wdg_status_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(wdg_status_label, 75, 153);

        /* 看门狗说明 */
        lv_obj_t * wdg_hint = lv_label_create(scr_set);
        lv_label_set_text(wdg_hint, "Timeout: ~1 second");
        lv_obj_set_style_text_color(wdg_hint, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(wdg_hint, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(wdg_hint, 15, 180);
    }

    /* 第2步：定时器（用 raw 坐标检测触摸） */
    lv_timer_create(data_update_timer_cb, 100, NULL);

    /* 第4步：启动 UART 接收 */
    HAL_UART_Receive_IT(&huart4, &uart_rx_byte, 1);

    /* 第5步：启动 USART1 (ESP32S3 WiFi) 接收 */
    HAL_UART_Receive_IT(&huart1, &wifi_rx_byte, 1);

    /* 第6步：显示主界面 */
    lv_screen_load(scr_main);
}
