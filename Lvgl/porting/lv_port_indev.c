/**
 * @file lv_port_indev.c
 * 触摸输入设备移植：FT6206 电容触摸芯片（I2C 接口）
 */

/* 启用本文件（原来是 0，现在改成 1）*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"   // 声明 lv_port_indev_init()
#include "main.h"            // CTP_RST_Pin / CTP_INT_Pin 引脚宏
#include "i2c.h"             // hi2c1 句柄

/*********************
 *      DEFINES
 *********************/
/* FT6206 的 I2C 7位地址是 0x38。
 * 注意：HAL 库的地址参数要"左移 1 位"（腾出最低位给读/写标志），所以写 (0x38 << 1) = 0x70 */
#define FT6206_ADDR          (0x38 << 1)

/* FT6206 寄存器地址：0x02 是"触摸点数状态"寄存器，
 * 从它开始连续 5 个字节就是：点数 + 触摸点1的X(2字节) + 触摸点1的Y(2字节) */
#define FT6206_REG_TD_STATUS 0x02

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_indev_t * indev_touchpad;
static int32_t g_raw_x = 0;    // 全局：最新的原始X坐标
static int32_t g_raw_y = 0;    // 全局：最新的原始Y坐标
static int32_t g_lvgl_x = 0;   // 全局：LVGL 转换后的 X 坐标
static int32_t g_lvgl_y = 0;   // 全局：LVGL 转换后的 Y 坐标
static bool  g_pressed = false; // 全局：是否按下

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void touchpad_init(void);
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data);
static bool ft6206_read_xy(int32_t * x, int32_t * y, int32_t * raw_x_out, int32_t * raw_y_out);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/* 初始化触摸输入设备，并注册给 LVGL */
void lv_port_indev_init(void)
{
    /* 1. 先初始化触摸芯片硬件 */
    touchpad_init();

    /* 2. 创建一个"输入设备"对象 */
    indev_touchpad = lv_indev_create();

    /* 3. 类型选"指针"（鼠标/触摸屏都算指针类，靠坐标定位） */
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);

    /* 4. 最关键一步：把"读触摸"的函数交给 LVGL。
     *    之后 LVGL 每隔一段时间就自动调用 touchpad_read() 来更新触摸状态 */
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}

/* 获取最新的触摸坐标 */
void lv_port_indev_get_raw(int32_t * raw_x, int32_t * raw_y, bool * pressed)
{
    *raw_x = g_raw_x;
    *raw_y = g_raw_y;
    *pressed = g_pressed;
}

/* 获取 LVGL 转换后的坐标（与 LVGL 显示坐标系一致） */
void lv_port_indev_get_lvgl(int32_t * x, int32_t * y, bool * pressed)
{
    *x = g_lvgl_x;
    *y = g_lvgl_y;
    *pressed = g_pressed;
}

/* 命中检测：直接用 raw 坐标判断按了哪个按钮（返回0~8，-1=没按到）
 *
 * 按钮布局（raw 坐标系）：
 *   按钮0=ADC   按钮1=DAC   按钮2=PWM
 *   按钮3=UART  按钮4=CAM   按钮5=WIFI
 *   按钮6=TASK  按钮7=SET   按钮8=HOME
 *
 * raw 坐标范围：
 *   行0 (ADC,DAC,PWM):     raw_x 30-70
 *   行1 (UART,CAM,WIFI):   raw_x 90-130
 *   行2 (TASK,SET,HOME):   raw_x 150-190
 *   列0 (ADC,90,TASK):     raw_y 30-90
 *   列1 (DAC,CAM,SET):     raw_y 130-190
 *   列2 (PWM,WIFI,HOME):   raw_y 230-290
 */
int lv_port_indev_hit_test(int32_t raw_x, int32_t raw_y)
{
    /* 判断行：raw_x 在哪个范围 */
    int row = -1;
    if(raw_x >= 30  && raw_x <= 70)  row = 0;   // 行0: ADC, DAC, PWM
    if(raw_x >= 90  && raw_x <= 130) row = 1;   // 行1: UART, CAM, WIFI
    if(raw_x >= 150 && raw_x <= 190) row = 2;   // 行2: TASK, SET, HOME

    /* 判断列：raw_y 在哪个范围 */
    int col = -1;
    if(raw_y >= 30  && raw_y <= 90)  col = 0;   // 列0: ADC, UART, TASK
    if(raw_y >= 130 && raw_y <= 190) col = 1;   // 列1: DAC, CAM, SET
    if(raw_y >= 230 && raw_y <= 290) col = 2;   // 列2: PWM, WIFI, HOME

    if(row < 0 || col < 0) return -1;   // 没按到任何按钮
    return row * 3 + col;               // 返回0~8
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* 初始化触摸芯片：给 FT6206 一个复位脉冲 */
static void touchpad_init(void)
{
    /* RST 脚先拉低、再拉高，完成一次复位 */
    HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

/* LVGL 会反复调用这个函数"读触摸"，把最新的状态和坐标填进 data */
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    static int32_t last_x = 0;   // 用 static 记住上次坐标，松手时坐标还留在原地
    static int32_t last_y = 0;
    static int32_t raw_x = 0;    // 调试用：记录原始坐标
    static int32_t raw_y = 0;

    /* 读一次触摸芯片：返回是否有手指按着，并把坐标写进 last_x/last_y */
    bool pressed = ft6206_read_xy(&last_x, &last_y, &raw_x, &raw_y);

    /* 保存到全局变量 */
    g_raw_x = raw_x;
    g_raw_y = raw_y;
    g_lvgl_x = last_x;  /* 现在 lvgl_x = raw_x */
    g_lvgl_y = last_y;  /* 现在 lvgl_y = raw_y */
    g_pressed = pressed;

    /* 把"按没按"告诉 LVGL */
    if(pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* 把坐标告诉 LVGL */
    data->point.x = last_x;
    data->point.y = last_y;
}

/* 通过 I2C 读 FT6206：一次读回触摸点数和坐标
 * 参数说明：
 *   x, y        → 输出：raw 坐标（与 LVGL 坐标相同，不做映射）
 *   raw_x_out   → 输出：FT6206 原始X坐标
 *   raw_y_out   → 输出：FT6206 原始Y坐标
 */
static bool ft6206_read_xy(int32_t * x, int32_t * y, int32_t * raw_x_out, int32_t * raw_y_out)
{
    uint8_t buf[5];

    /* 从寄存器 0x02 开始，连续读 5 个字节：
     *   buf[0] = TD_STATUS（触摸点数）
     *   buf[1] = P1_XH，buf[2] = P1_XL（触摸点1的 X 坐标）
     *   buf[3] = P1_YH，buf[4] = P1_YL（触摸点1的 Y 坐标） */
    if(HAL_I2C_Mem_Read(&hi2c1, FT6206_ADDR, FT6206_REG_TD_STATUS,
                        I2C_MEMADD_SIZE_8BIT, buf, 5, 100) != HAL_OK) {
        return false;   // I2C 读失败，当作"没触摸"（避免程序卡死）
    }

    /* 触摸点数 = TD_STATUS 的低 4 位，等于 0 表示没有手指 */
    if((buf[0] & 0x0F) == 0) {
        return false;
    }

    /* 拼出 12 位坐标：高 4 位藏在 XH/YH 的低 4 位里，低 8 位在 XL/YL 里 */
    int32_t raw_x = ((buf[1] & 0x0F) << 8) | buf[2];
    int32_t raw_y = ((buf[3] & 0x0F) << 8) | buf[4];

    /* 保存原始坐标供调试 */
    *raw_x_out = raw_x;
    *raw_y_out = raw_y;

    /* 直接使用 raw 坐标，不做映射 */
    *x = raw_x;
    *y = raw_y;

    /* 限幅：确保坐标在屏幕范围内 */
    if(*x < 0) *x = 0;
    if(*x > 239) *x = 239;
    if(*y < 0) *y = 0;
    if(*y > 319) *y = 319;

    return true;
}

#else /* 文件顶部的 #if 0 会走到这里 */

/* 这个 typedef 只是为了消除编译警告 */
typedef int keep_pedantic_happy;

#endif
