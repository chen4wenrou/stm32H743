# 触摸屏与 LVGL 输入设备驱动详解

## 📖 你将学到什么

读完这篇文档，你将彻底理解：
1.  触摸屏的工作原理（从物理层到数据层）
2.  FT6206 电容触摸芯片的寄存器与通信协议
3.  LVGL 输入设备（indev）的架构与回调机制
4.  **为什么屏幕旋转后触摸会错位，以及如何修复**
5.  如何调试和校准触摸坐标

---

## 第一章：触摸屏基础——它到底是什么？

### 1.1 触摸屏的两层结构

你可以把触摸屏想象成**两层叠在一起的"膜"**：

```
┌─────────────────────────────────────┐
│          你看到的画面（TFT LCD）       │  ← 负责显示
├─────────────────────────────────────┤
│          触摸感应层（Touch Panel）     │  ← 负责感知手指位置
└─────────────────────────────────────┘
```

- **TFT LCD 层**：就是你已经熟悉的 ILI9341，通过 SPI 接收像素数据，点亮液晶。
- **触摸感应层**：一片透明的导电薄膜，贴在 LCD 上面。它有自己的控制芯片（这里是 **FT6206**），通过 **I2C** 接口和主控（STM32）通信。

**关键点**：这两层是**完全独立**的系统，只是物理上叠在一起。LCD 不知道触摸的存在，触摸芯片也不知道 LCD 显示了什么。

### 1.2 电容触摸的原理（通俗版）

你身上有微弱的电荷。当你的手指靠近屏幕表面的导电层时，会改变那个区域的电容值。触摸芯片不断扫描整个导电层，检测哪里的电容发生了变化，从而确定你手指的 X、Y 坐标。

```
你的手指 ──→ 改变电容 ──→ FT6206 检测到 ──→ 算出坐标 ──→ 通过 I2C 报告给 STM32
```

---

## 第二章：FT6206 芯片详解

### 2.1 芯片基本信息

| 项目 | 值 |
|------|-----|
| 芯片型号 | FT6206 |
| 通信接口 | I2C |
| 7位地址 | **0x38**（HAL 库写成 0x70） |
| 最大分辨率 | 约 4096×4096（内部 12 位精度） |
| 支持触摸点数 | 最多 2 点 |

### 2.2 I2C 地址的坑（为什么是 0x70？）

```
FT6206 的 7 位地址 = 0x38 = 0b0111000

HAL 库的 I2C 函数要求地址"左移 1 位"，把最低位留给读/写标志：

    0b0111000  (0x38)  ← 7位地址
       ↓ 左移1位
    0b01110000 (0x70)  ← HAL 库要的格式
    ^^^^^^^^
    |      |
    7位地址  读写位(硬件自动填)
```

所以代码里写 `(0x38 << 1)` 即 `0x70`，这是 HAL 库的标准做法，不是 bug。

### 2.3 关键寄存器：0x02 TD_STATUS

FT6206 有很多寄存器，但我们只关心 **0x02 开始的 5 个字节**：

```
寄存器地址    名称        内容
─────────────────────────────────────────
0x02        TD_STATUS   触摸点数（低4位有效）
0x03        P1_XH       触摸点1的 X 高4位（低4位）
0x04        P1_XL       触摸点1的 X 低8位
0x05        P1_YH       触摸点1的 Y 高4位（低4位）
0x06        P1_YL       触摸点1的 Y 低8位
```

一次 I2C 连续读 5 字节，就能拿到所有需要的信息。

### 2.4 坐标拼接：12 位坐标怎么拼出来的？

触摸芯片内部用 12 位精度记录坐标（0~4095），但一个字节只有 8 位，所以需要两个字节来拼：

```
以 X 坐标为例：

buf[1] = P1_XH = 0b????XXXX  （低4位是X的高4位）
buf[2] = P1_XL = 0bXXXXXXXX  （全部8位是X的低8位）

拼接过程：
    raw_x = ((buf[1] & 0x0F) << 8) | buf[2]
            ─────────────────────    ───────
            取低4位，左移到高4位     低8位直接拼

举例：
    buf[1] = 0x2A = 0b00101010
    buf[2] = 0x56 = 0b01010110

    buf[1] & 0x0F = 0x0A = 0b1010
    左移8位        = 0x0A00 = 0b0000101000000000
    或上 buf[2]    = 0x0A56 = 0b0000101001010110

    raw_x = 0x0A56 = 2646（12位坐标值）
```

### 2.5 I2C 读取时序图

```
STM32（主）                      FT6206（从）
    │                                │
    │──── S: START ─────────────────→│
    │──── ADDR+W (0x70) ────────────→│  告诉FT6206："我要写寄存器地址"
    │──── REG (0x02) ───────────────→│  "从0x02开始读"
    │──── S: RESTART ───────────────→│  切换方向
    │──── ADDR+R (0x71) ────────────→│  "现在我要读数据"
    │←─── buf[0] (TD_STATUS) ───────│  FT6206返回：点数
    │←─── buf[1] (P1_XH) ───────────│  返回：X高字节
    │←─── buf[2] (P1_XL) ───────────│  返回：X低字节
    │←─── buf[3] (P1_YH) ───────────│  返回：Y高字节
    │←─── buf[4] (P1_YL) ───────────│  返回：Y低字节
    │──── P: STOP ──────────────────→│
```

对应代码：
```c
HAL_I2C_Mem_Read(&hi2c1,        // I2C句柄
                 FT6206_ADDR,    // 0x70（0x38左移1位）
                 0x02,           // 起始寄存器地址
                 I2C_MEMADD_SIZE_8BIT,
                 buf,            // 接收缓冲区
                 5,              // 连续读5字节
                 100);           // 超时100ms
```

---

## 第三章：LVGL 输入设备（indev）架构

### 3.1 为什么需要"输入设备"抽象层？

LVGL 是一个 GUI 库，它不知道你的硬件是什么。它不关心你是用触摸屏、鼠标、键盘还是旋转编码器。

LVGL 说："你告诉我两件事就行：
1. **用户有没有按下？**（状态：pressed / released）
2. **如果按了，坐标在哪？**（x, y）"

然后 LVGL 自己去判断用户点到了哪个按钮、哪个滑块。

### 3.2 LVGL 的"回调"机制

这是理解整个触摸驱动的关键。LVGL 不会主动去读你的硬件，而是用**回调函数**的模式：

```
初始化时：
    你告诉 LVGL："这是我读触摸的函数"  ← lv_indev_set_read_cb()

运行时（每帧循环）：
    LVGL 内部："好，我需要知道触摸状态了"
       ↓
    LVGL 调用你写的 touchpad_read()
       ↓
    你的函数去读 I2C，填好 data->state 和 data->point
       ↓
    LVGL 拿到结果，判断用户点了什么
```

用代码来说就是这个结构：

```c
// 你写的回调函数
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    // 1. 读硬件，得到坐标和状态
    bool pressed = ft6206_read_xy(&last_x, &last_y);

    // 2. 填进 data 结构体，LVGL 会来取
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = last_x;
    data->point.y = last_y;
}
```

### 3.3 注册流程详解

```c
void lv_port_indev_init(void)
{
    // 第1步：初始化硬件（复位触摸芯片）
    touchpad_init();

    // 第2步：创建一个"输入设备"对象
    //        这就像在 LVGL 里"注册"了一个新设备
    indev_touchpad = lv_indev_create();

    // 第3步：告诉 LVGL 这是什么类型的设备
    //        POINTER = 指针类（靠坐标定位，触摸屏/鼠标都属于这类）
    //        其他类型还有：KEYPAD（键盘）、ENCODER（编码器）、BUTTON（按钮）
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);

    // 第4步：把"读触摸"的函数绑定给 LVGL
    //        之后 LVGL 会在每帧自动调用 touchpad_read()
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}
```

### 3.4 `lv_indev_data_t` 结构体详解

这个结构体是 LVGL 和你的驱动之间的"契约"：

```c
typedef struct {
    lv_point_t point;    // 触摸坐标 {x, y}
    lv_indev_state_t state;  // 按下/释放
    // ... 其他字段
} lv_indev_data_t;
```

- **`point.x`**：触摸点的 X 坐标（像素）
- **`point.y`**：触摸点的 Y 坐标（像素）
- **`state`**：`LV_INDEV_STATE_PRESSED` 或 `LV_INDEV_STATE_RELEASED`

**重要**：当手指松开时（state = RELEASED），LVGL 会继续使用上一次的 point 坐标。这就是为什么代码里用 `static int32_t last_x, last_y` 来"记住"上次位置。

---

## 第四章：屏幕旋转与坐标变换（核心难点！）

### 4.1 你的硬件配置

从代码中可以看到：
- **屏幕面板**：ILI9341，原生分辨率 **240×320**（竖屏）
- **显示旋转**：`LV_DISPLAY_ROTATION_90`，变成 **320×240**（横屏）
- **触摸芯片**：FT6206，贴在屏幕面板上

### 4.2 问题的根源

这里有一个**至关重要的事实**：

```
┌────────────────────────────────────────────────────────────────┐
│  屏幕旋转只改变了显示方向，触摸芯片完全不知道这件事！            │
│                                                                │
│  ILI9341 被告诉"你要旋转90度显示"                              │
│  FT6206 什么都不知道，它还是按原来的方向报坐标                  │
└────────────────────────────────────────────────────────────────┘
```

画个图来说明：

```
【物理屏幕的"自然"方向（竖屏）】

    触摸坐标系              显示坐标系（旋转后）
    X: 0→239 (宽)           X: 0→319 (宽)
    Y: 0→319 (高)           Y: 0→239 (高)

         Y轴                      Y轴
         ↑                        ↑
    319 ─┼──────            239 ─┼──────
         │    │                   │    │
         │ 屏 │                   │ 屏 │
         │ 幕 │                   │ 幕 │
       0 ┼────┼──→ X         0 ──┼────┼──→ X
         0   239                 0   319

    旋转90°后，原来的 Y 变成了 X，原来的 X 变成了 Y（并翻转）
```

### 4.3 坐标变换公式

当屏幕旋转90°时，物理坐标到显示坐标的变换：

```
物理坐标 (raw_x, raw_y)  →  显示坐标 (display_x, display_y)

display_x = raw_y                // 物理的Y → 显示的X
display_y = 239 - raw_x          // 物理的X → 显示的Y，取反
```

**为什么是 `239 - raw_x` 而不是 `raw_x`？**

想象你把手机竖着拿，然后顺时针旋转90°变成横屏：
- 原来屏幕**顶部**（Y=319）变成了横屏的**右侧**
- 原来屏幕**左侧**（X=0）变成了横屏的**顶部**
- 原来屏幕**底部**（Y=0）变成了横屏的**左侧**

所以 X 轴需要翻转（`239 - raw_x`），否则上下会颠倒。

### 4.4 四种旋转的变换公式速查表

| 旋转角度 | display_x | display_y |
|----------|-----------|-----------|
| 0°（原方向） | raw_x | raw_y |
| 90°（顺时针） | raw_y | 239 - raw_x |
| 180° | 239 - raw_x | 319 - raw_y |
| 270°（逆时针） | 319 - raw_y | raw_x |

**注意**：上面的 239 和 319 是面板原始分辨率减1。如果你的屏幕是 320×240，数字要相应调整。

### 4.5 你的代码中的变换

```c
// lv_port_indev.c 第129-130行
*x = raw_y;             // 横屏的 X 来自触摸的 Y
*y = 239 - raw_x;       // 横屏的 Y 来自触摸的 X，取反
```

这个变换对应的是**顺时针旋转90°**的情况。

---

## 第五章：调试触摸坐标——你的问题怎么解决

### 5.1 诊断方法

在 `touchpad_read` 函数里加打印，看看实际的坐标：

```c
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    static int32_t last_x = 0;
    static int32_t last_y = 0;

    bool pressed = ft6206_read_xy(&last_x, &last_y);

    if(pressed) {
        // ★ 调试用：打印原始坐标和转换后的坐标
        // 用串口助手观察，按屏幕四个角看坐标范围
        printf("Touch: display(%ld, %ld)\n", last_x, last_y);
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}
```

### 5.2 坐标错位的典型现象与修复

| 你看到的现象 | 原因 | 修复方法 |
|-------------|------|---------|
| 按上面，下面亮 | Y轴反了 | `*y = 319 - raw_y` 或调整公式 |
| 按左边，右边亮 | X轴反了 | `*x = 239 - raw_y` 或调整公式 |
| X和Y对调了 | 旋转方向搞反了 | 交换 `*x` 和 `*y` 的赋值 |
| 坐标范围不对 | 原始坐标超出屏幕尺寸 | 检查 raw 值范围，可能需要缩放 |

### 5.3 系统化的调试步骤

**步骤1**：先不管旋转，读原始坐标
```c
// 临时代码，调试完删掉
printf("raw: x=%ld y=%ld\n", raw_x, raw_y);
```
按屏幕四个角，记下原始坐标的范围（比如 X: 0~239, Y: 0~319）。

**步骤2**：确定变换公式
根据原始坐标范围和你想要的显示坐标范围，套用第四章的公式。

**步骤3**：验证
按屏幕四角，确认显示坐标正确映射到 LVGL 的 (0,0) 到 (319,239)。

### 5.4 你的情况分析

你说"按9号按键，3号按键变红"：

```
数字键盘布局（假设）：
┌───┬───┬───┐
│ 7 │ 8 │ 9 │    ← 顶部
├───┼───┼───┤
│ 4 │ 5 │ 6 │    ← 中部
├───┼───┼───┤
│ 1 │ 2 │ 3 │    ← 底部
└───┴───┴───┘
```

按 9（右上角）却触发 3（右下角），说明**Y轴方向反了**。

修复方案：把第130行从
```c
*y = 239 - raw_x;
```
改成
```c
*y = raw_x;       // 不取反，或者根据实际调试结果调整
```

或者如果你发现同时X也反了，可能需要用另一个旋转角度的公式。

---

## 第六章：完整代码逐行注释

```c
/**
 * @file lv_port_indev.c
 * 触摸输入设备移植：FT6206 电容触摸芯片（I2C 接口）
 */

/* 启用本文件 */
#if 1

/* ========== 头文件 ========== */
#include "lv_port_indev.h"   // 声明 lv_port_indev_init()
#include "main.h"            // CTP_RST_Pin / CTP_INT_Pin 引脚宏
#include "i2c.h"             // hi2c1 句柄

/* ========== 宏定义 ========== */

/* FT6206 的 I2C 地址
 * 7位地址 = 0x38
 * HAL库格式 = 0x38 << 1 = 0x70（最低位留给读写标志） */
#define FT6206_ADDR          (0x38 << 1)

/* 起始寄存器：0x02 = TD_STATUS（触摸点数）
 * 从这里连续读5字节：点数 + X(2字节) + Y(2字节) */
#define FT6206_REG_TD_STATUS 0x02

/* ========== 静态变量 ========== */
static lv_indev_t * indev_touchpad;

/* ========== 函数声明 ========== */
static void touchpad_init(void);
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data);
static bool ft6206_read_xy(int32_t * x, int32_t * y);

/* ========== 全局函数 ========== */

void lv_port_indev_init(void)
{
    /* 第1步：硬件初始化（复位FT6206） */
    touchpad_init();

    /* 第2步：在LVGL中创建输入设备 */
    indev_touchpad = lv_indev_create();

    /* 第3步：设置类型为"指针"（触摸屏/鼠标都算指针类） */
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);

    /* 第4步：绑定读取回调函数
     * LVGL每帧会自动调用 touchpad_read() */
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}

/* ========== 静态函数 ========== */

/* 硬件初始化：给FT6206一个复位脉冲 */
static void touchpad_init(void)
{
    /* 复位时序：拉低10ms → 拉高10ms */
    HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

/* LVGL回调：读取触摸状态
 *
 * LVGL 每帧都会调用这个函数，你需要：
 * 1. 判断有没有手指按下
 * 2. 如果有，读取坐标并填入 data->point
 * 3. 设置 data->state
 *
 * 注意：松手时，坐标保持上次的值（用static变量记住） */
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    static int32_t last_x = 0;   // 松手时坐标不变，所以用static记住
    static int32_t last_y = 0;

    /* 读FT6206，返回是否有触摸 */
    bool pressed = ft6206_read_xy(&last_x, &last_y);

    /* 设置按下/释放状态 */
    if(pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* 设置坐标（无论按下还是释放，都填坐标） */
    data->point.x = last_x;
    data->point.y = last_y;
}

/* 通过I2C读取FT6206的坐标
 *
 * 通信流程：
 * 1. 从寄存器0x02连续读5字节
 * 2. 解析触摸点数（buf[0]的低4位）
 * 3. 拼接12位坐标（高4位 + 低8位）
 * 4. 坐标变换：触摸坐标 → 显示坐标
 */
static bool ft6206_read_xy(int32_t * x, int32_t * y)
{
    uint8_t buf[5];

    /* I2C连续读5字节：
     * buf[0] = TD_STATUS  点数
     * buf[1] = P1_XH      X高字节
     * buf[2] = P1_XL      X低字节
     * buf[3] = P1_YH      Y高字节
     * buf[4] = P1_YL      Y低字节 */
    if(HAL_I2C_Mem_Read(&hi2c1, FT6206_ADDR, FT6206_REG_TD_STATUS,
                        I2C_MEMADD_SIZE_8BIT, buf, 5, 100) != HAL_OK) {
        return false;   // I2C失败，当作没触摸
    }

    /* 检查触摸点数：低4位为0表示没有手指 */
    if((buf[0] & 0x0F) == 0) {
        return false;
    }

    /* 拼接12位坐标：
     * 高4位在XH/YH的低4位里，低8位在XL/YL里 */
    int32_t raw_x = ((buf[1] & 0x0F) << 8) | buf[2];
    int32_t raw_y = ((buf[3] & 0x0F) << 8) | buf[4];

    /* ========== 坐标变换（屏幕旋转90°） ==========
     *
     * 屏幕原生240×320（竖屏），旋转90°后变320×240（横屏）
     * 触摸芯片报的是"物理坐标"，需要转换为"显示坐标"
     *
     * 旋转90°的变换公式：
     *   显示X = 物理Y
     *   显示Y = 239 - 物理X
     *
     * 如果点不准，按下面的表调整：
     *   上下颠倒 → 改 y 公式
     *   左右颠倒 → 改 x 公式
     *   X/Y 对调 → 交换两行 */
    *x = raw_y;             // 显示X 来自 触摸Y
    *y = 239 - raw_x;       // 显示Y 来自 触摸X（取反）

    return true;
}

#else
typedef int keep_pedantic_happy;
#endif
```

---

## 第七章：进阶知识

### 7.1 为什么 `last_x/last_y` 要用 `static`？

```c
static int32_t last_x = 0;
static int32_t last_y = 0;
```

`static` 让这两个变量在函数调用之间**保持值**。

**场景**：用户按了一下按钮然后松手
- 按下时：`ft6206_read_xy` 返回 true，坐标更新到 (100, 50)
- 松手后：`ft6206_read_xy` 返回 false，但 last_x/last_y 还是 (100, 50)

LVGL 需要知道"用户最后触摸的位置"来判断"在哪个按钮上松手的"。如果松手时坐标突然变成 (0,0)，LVGL 会认为用户在 (0,0) 松手，而不是在按钮上松手。

### 7.2 触摸采样率与 LVGL 的刷新率

LVGL 的主循环（`lv_timer_handler`）每帧都会调用 `touchpad_read`。

```
LVGL 刷新率（比如 33ms/帧 ≈ 30fps）
       ↓
每帧调用一次 touchpad_read()
       ↓
读一次 I2C（约 0.1ms）
       ↓
返回坐标给 LVGL
```

触摸采样率由 LVGL 的刷新率决定，通常 30fps 足够流畅。

### 7.3 FT6206 vs XPT2046 对比

你的项目用的是 FT6206（电容触摸），但很多教程讲的是 XPT2046（电阻触摸），这里对比一下：

| 特性 | FT6206 (电容) | XPT2046 (电阻) |
|------|---------------|----------------|
| 接口 | **I2C** | SPI |
| 触摸方式 | 手指轻触 | 需要用力按压 |
| 坐标精度 | 高（12位） | 中（12位） |
| 多点触摸 | 支持2点 | 不支持 |
| 扫描方式 | 自动扫描 | 需要主机主动发起转换 |

### 7.4 坐标缩放（如果原始坐标范围不是 0~239 / 0~319）

有些触摸面板的原始坐标范围可能不是精确的 0~239 或 0~319，而是类似 20~220。这时需要**线性映射**：

```c
// 原始范围：raw_min ~ raw_max
// 目标范围：0 ~ (屏幕宽度-1)

// 线性映射公式
int32_t map(int32_t raw, int32_t raw_min, int32_t raw_max, int32_t disp_min, int32_t disp_max) {
    return (raw - raw_min) * (disp_max - disp_min) / (raw_max - raw_min) + disp_min;
}

// 使用示例
*x = map(raw_y, 20, 220, 0, 319);
*y = map(raw_x, 20, 220, 0, 239);
```

---

## 第八章：总结与调试清单

### 初始化流程总结

```
main()
  ↓
lv_port_display_init()        // 初始化显示
  ├─ 创建 ILI9341 驱动
  ├─ 设置旋转 90°（横屏）
  └─ 开启颜色反转
  ↓
lv_port_indev_init()          // 初始化触摸
  ├─ 复位 FT6206
  ├─ 创建输入设备
  ├─ 设置类型为 POINTER
  └─ 绑定 touchpad_read 回调
  ↓
while(1) {
    lv_timer_handler();       // LVGL主循环
    // 每帧自动调用 touchpad_read()
}
```

### 调试清单

- [ ] 确认 I2C 通信正常（`HAL_I2C_Mem_Read` 返回 `HAL_OK`）
- [ ] 确认触摸点数 > 0（`(buf[0] & 0x0F) != 0`）
- [ ] 打印原始坐标 raw_x/raw_y，确认范围合理
- [ ] 打印变换后的坐标 *x/*y，确认范围是 0~319 / 0~239
- [ ] 按屏幕四角，确认坐标映射正确
- [ ] 确认 LVGL 的显示分辨率是 320×240（旋转后）

---

## 附录：关键数据结构速查

### `lv_indev_data_t`

```c
typedef struct _lv_indev_data_t {
    lv_point_t point;           // 触摸坐标 {x, y}
    lv_indev_state_t state;     // LV_INDEV_STATE_PRESSED 或 RELEASED
    // ... 其他字段（按键值、编码器差异等，触摸不用关心）
} lv_indev_data_t;
```

### `lv_indev_state_t`

```c
enum {
    LV_INDEV_STATE_RELEASED = 0,   // 松手
    LV_INDEV_STATE_PRESSED = 1     // 按下
};
```

### FT6206 寄存器映射（常用部分）

```
地址    名称            说明
0x00    DEV_MODE        设备模式（一般不用改）
0x01    GEST_ID         手势ID（本项目未用）
0x02    TD_STATUS       触摸点数（我们读这个开始）
0x03    P1_XH           触摸点1 X高位
0x04    P1_XL           触摸点1 X低位
0x05    P1_YH           触摸点1 Y高位
0x06    P1_YL           触摸点1 Y低位
0x07    P1_WEIGHT       触摸点1 压力权重（本项目未用）
0x08    P1_MISC         触摸点1 杂项（本项目未用）
0x09~0x0E  P2_*         触摸点2 的数据（本项目未用）
```

---

*文档版本：v1.0*
*适用项目：STM32H743 + ILI9341 + FT6206 + LVGL*
*最后更新：2025年*