/*
 * WiFi 动画示例
 *
 * 这个文件展示了如何在 LVGL 中创建 WiFi 动画效果
 * 有两种方法：
 * 1. 使用 spinner 控件（简单，推荐）
 * 2. 使用 GIF 文件（需要 LV_USE_GIF=1）
 */

#include "lvgl.h"

/* ================================================================
 * 方法1：使用 spinner 控件（当前实现）
 * ================================================================ */

/*
 * 优点：
 * - 简单易用
 * - 不需要额外资源
 * - 内存占用小
 *
 * 缺点：
 * - 只能显示简单的旋转动画
 * - 无法显示复杂的 WiFi 图标
 */

void create_wifi_spinner_example(lv_obj_t * parent)
{
    /* 创建 spinner */
    lv_obj_t * spinner = lv_spinner_create(parent);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_center(spinner);

    /* 设置样式 */
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x00FF00), 0);  // 绿色
    lv_obj_set_style_arc_width(spinner, 4, 0);
    lv_obj_set_style_arc_rounded(spinner, true, 0);

    /* 设置动画参数：1秒一圈 */
    lv_spinner_set_anim_params(spinner, 1000, 200);

    /* 添加中心点 */
    lv_obj_t * dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_radius(dot, 5, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
}

/* ================================================================
 * 方法2：使用 GIF 文件
 * ================================================================ */

/*
 * 优点：
 * - 可以显示复杂的动画
 * - 视觉效果好
 *
 * 缺点：
 * - 需要 LV_USE_GIF=1
 * - 占用较多内存
 * - 需要准备 GIF 文件
 */

// 使用方法：
// 1. 准备一个 WiFi 图标的 GIF 文件
// 2. 使用 LVGL 的图片转换工具将 GIF 转换为 C 数组
// 3. 在代码中使用：

/*
// 声明 GIF 资源
LV_IMAGE_DECLARE(wifi_animation);

void create_wifi_gif_example(lv_obj_t * parent)
{
    // 创建 GIF 控件
    lv_obj_t * gif = lv_gif_create(parent);
    lv_obj_set_size(gif, 60, 60);
    lv_obj_center(gif);

    // 设置 GIF 源
    lv_gif_set_src(gif, &wifi_animation);

    // 控制播放
    lv_gif_set_loop_count(gif, -1);  // 无限循环
}
*/

/* ================================================================
 * 方法3：使用多个图片切换（手动动画）
 * ================================================================ */

/*
 * 优点：
 * - 不需要 GIF 支持
 * - 可以精确控制动画
 *
 * 缺点：
 * - 需要准备多张图片
 * - 代码复杂
 */

// 使用方法：
// 1. 准备多张 WiFi 图标图片（不同状态）
// 2. 使用定时器切换显示

/*
// 声明图片资源
LV_IMAGE_DECLARE(wifi_frame_0);
LV_IMAGE_DECLARE(wifi_frame_1);
LV_IMAGE_DECLARE(wifi_frame_2);

static lv_obj_t * wifi_img;
static int current_frame = 0;

static void wifi_anim_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    current_frame = (current_frame + 1) % 3;

    switch(current_frame) {
        case 0:
            lv_image_set_src(wifi_img, &wifi_frame_0);
            break;
        case 1:
            lv_image_set_src(wifi_img, &wifi_frame_1);
            break;
        case 2:
            lv_image_set_src(wifi_img, &wifi_frame_2);
            break;
    }
}

void create_wifi_manual_anim_example(lv_obj_t * parent)
{
    wifi_img = lv_image_create(parent);
    lv_obj_set_size(wifi_img, 60, 60);
    lv_obj_center(wifi_img);
    lv_image_set_src(wifi_img, &wifi_frame_0);

    // 创建定时器，每500ms切换一次
    lv_timer_create(wifi_anim_timer_cb, 500, NULL);
}
*/

/* ================================================================
 * 创建自定义 WiFi 图标（使用 LVGL 绘图）
 * ================================================================ */

/*
 * 使用 LVGL 的 canvas 控件绘制自定义 WiFi 图标
 */

// WiFi 信号强度数据（3层信号）
static const uint8_t wifi_signal_data[3][8] = {
    // 最内层：信号强度1
    {
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00011000,
        0b00111100,
        0b01111110,
    },
    // 中层：信号强度2
    {
        0b00000000,
        0b00000000,
        0b00000000,
        0b00111100,
        0b01111110,
        0b11111111,
        0b11111111,
        0b11111111,
    },
    // 最外层：信号强度3
    {
        0b00111100,
        0b01111110,
        0b11111111,
        0b11111111,
        0b11111111,
        0b11111111,
        0b11111111,
        0b11111111,
    },
};

void create_custom_wifi_icon_example(lv_obj_t * parent)
{
    // 创建 canvas
    lv_obj_t * canvas = lv_canvas_create(parent);
    lv_obj_set_size(canvas, 60, 60);
    lv_obj_center(canvas);

    // 分配缓冲区
    static uint8_t buf[60 * 60 * 4];  // ARGB8888
    lv_canvas_set_buffer(canvas, buf, 60, 60, LV_COLOR_FORMAT_ARGB8888);

    // 清空画布
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    // 绘制 WiFi 图标
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0x00FF00);
    rect_dsc.bg_opa = LV_OPA_COVER;

    // 绘制中心点
    lv_canvas_draw_rect(canvas, 25, 50, 10, 10, &rect_dsc);

    // 绘制信号波瓣
    for(int i = 0; i < 3; i++) {
        for(int y = 0; y < 8; y++) {
            for(int x = 0; x < 8; x++) {
                if(wifi_signal_data[i][y] & (1 << x)) {
                    int px = 20 + x * 2 + i * 4;
                    int py = 40 - y * 2 - i * 4;
                    lv_canvas_draw_rect(canvas, px, py, 2, 2, &rect_dsc);
                }
            }
        }
    }
}
