# STM32H743 + LVGL 项目踩坑记录

## 项目概述
- **MCU**: STM32H743
- **屏幕**: ILI9341 (240×320)，通过 `LV_DISPLAY_ROTATION_270` 旋转为 320×240 横屏显示
- **触摸**: FT6206 电容触摸芯片（I2C 接口）
- **UI框架**: LVGL v9

---

## 核心坑点1：两套坐标系

### 问题描述
LVGL 有自己的坐标系（旋转后 320×240），但触摸芯片返回的是 **raw 坐标**，两者不一致。

### 解决方案
**放弃 LVGL 坐标系，完全使用 raw 坐标系进行触摸检测。**

- **显示布局**：用 LVGL 坐标（`lv_obj_set_pos`）
- **触摸检测**：用 raw 坐标（直接读 FT6206）

### 代码实现
```c
/* 只读 raw 坐标 */
int32_t raw_x, raw_y;
bool pressed;
lv_port_indev_get_raw(&raw_x, &raw_y, &pressed);

/* 用 raw 坐标判断触摸区域 */
if(raw_x >= 30 && raw_x <= 70 && raw_y >= 230 && raw_y <= 290) {
    // HOME 按钮被按下
}
```

---

## 核心坑点2：raw 坐标系方向

### 实测的 raw 坐标系方向
```
raw_x: 0 (底部) → 240 (顶部)  垂直方向，从下到上
raw_y: 0 (右边) → 320 (左边)  水平方向，从右到左
```

### 校准方法
触摸屏幕四个角，记录 raw 坐标：
- 左上角: raw(26, 319) → LVGL(0, 0)
- 右上角: raw(14, 2)   → LVGL(280, 0)
- 左下角: raw(239, 316) → LVGL(0, 237)
- 右下角: raw(220, 0)  → LVGL(281, 213)

---

## 核心坑点3：触摸检测范围必须实测

### 调试方法
在屏幕上显示 raw 坐标：
```c
if(debug_label && pressed) {
    lv_label_set_text_fmt(debug_label, "raw:%ld,%ld", raw_x, raw_y);
}
```

### 实测结果（本项目最终按钮布局）

| 按钮 | raw_x 范围 | raw_y 范围 | 位置 |
|------|------------|------------|------|
| ADC | 150-190 | 30-90 | 上面一行，左边 |
| DAC | 150-190 | 130-190 | 上面一行，中间 |
| PWM | 150-190 | 230-290 | 上面一行，右边 |
| UART | 90-130 | 30-90 | 中间一行，左边 |
| CAM | 90-130 | 130-190 | 中间一行，中间 |
| WIFI | 90-130 | 230-290 | 中间一行，右边 |
| TASK | 30-70 | 30-90 | 下面一行，左边 |
| SET | 30-70 | 130-190 | 下面一行，中间 |
| **HOME** | **30-70** | **230-290** | 下面一行，右边 |

---

## 核心坑点4：HOME 按钮切换逻辑

### 实现要点
1. 初始状态：只显示 HOME 按钮，其他 8 个隐藏
2. 点击 HOME：切换 `btn_visible` 状态，显示/隐藏其他按钮
3. 其他按钮：只有在 `btn_visible = true` 时才能点击

```c
/* HOME 按钮：不依赖 btn_visible，直接切换 */
if(raw_x >= 30 && raw_x <= 70 && raw_y >= 230 && raw_y <= 290) {
    home_btn_cb(NULL);
}
/* 其他按钮：依赖 btn_visible */
else if(btn_visible) {
    // 检测其他按钮...
}
```

---

## 核心坑点5：触摸检测防抖

使用 `was_pressed` 标志位，只在按下瞬间触发一次：
```c
static bool was_pressed = false;
if(pressed) {
    if(!was_pressed) {
        was_pressed = true;
        // 只在按下瞬间执行一次
    }
} else {
    was_pressed = false;
}
```

---

## 核心坑点6：页面切换使用动画

```c
lv_screen_load_anim(scr_adc, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
```

---

## 核心坑点7：RAM 分区注意事项

- **DTCMRAM**：不能用 DMA 访问！只能 CPU 直接访问
- **RAM_D3**：可以用 DMA 访问，适合放 canvas 缓冲区

---

## 核心坑点8：栈空间不足

在 `FreeRTOSConfig.h` 中增大栈空间：
```c
#define configMINIMAL_STACK_SIZE 2048
```

---

## 核心坑点9：颜色反转

```c
lv_ili9341_set_invert(lcd_disp, true);
```

---

## 核心坑点10：LVGL 内存配置

在 `lv_conf.h` 中增大内存：
```c
#define LV_MEM_SIZE (192 * 1024U)  /* 192KB */
```

---

## 调试技巧

```c
/* 显示触摸坐标 */
if(debug_label && pressed) {
    lv_label_set_text_fmt(debug_label, "raw:%ld,%ld", raw_x, raw_y);
}

/* 显示按钮状态 */
if(debug_label) {
    lv_label_set_text(debug_label, btn_visible ? "HOME: SHOW" : "HOME: HIDE");
}
```

---

## 核心坑点11：USART1 中断缺失

### 问题描述
CubeMX 配置 USART1 时使用了 DMA 模式，但代码中使用 `HAL_UART_Receive_IT()` 中断接收模式。中断模式需要：
1. USART1_IRQHandler 中断处理函数
2. NVIC 中使能 USART1 中断

### 解决方案

**1. 添加中断处理函数** (`Core/Src/stm32h7xx_it.c`)
```c
extern UART_HandleTypeDef huart1;

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}
```

**2. 启用 NVIC 中断** (`Core/Src/usart.c`)
在 `HAL_UART_MspInit()` 的 USART1 部分添加：
```c
/* USART1 interrupt Init */
HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
HAL_NVIC_EnableIRQ(USART1_IRQn);
```

---

## 核心坑点12：UART 消息队列

### 问题描述
ESP32 发送多个消息间隔太短时，后到的消息会覆盖先到的消息。

### 解决方案
使用消息队列保存所有收到的消息：
```c
#define WIFI_MSG_QUEUE_SIZE 4
static volatile uint8_t wifi_msg_count = 0;
static uint8_t wifi_msg_head = 0;
static uint8_t wifi_msg_tail = 0;
static uint8_t wifi_msg_payload[WIFI_MSG_QUEUE_SIZE][WIFI_FRAME_MAX_PAYLOAD];
static uint8_t wifi_msg_len[WIFI_MSG_QUEUE_SIZE];
```

接收时放入队列：
```c
uint8_t next_head = (wifi_msg_head + 1) % WIFI_MSG_QUEUE_SIZE;
if(next_head != wifi_msg_tail) {
    memcpy(wifi_msg_payload[wifi_msg_head], data, len);
    wifi_msg_len[wifi_msg_head] = len;
    wifi_msg_head = next_head;
    wifi_msg_count++;
}
```

主循环处理队列：
```c
while(wifi_msg_count > 0) {
    // 从队列中读取消息
    uint8_t tail = wifi_msg_tail;
    // 处理消息...
    wifi_msg_tail = (tail + 1) % WIFI_MSG_QUEUE_SIZE;
    wifi_msg_count--;
}
```

---

## ESP32-S3 串口通信协议

### 串口参数
- 波特率：115200
- 数据位：8
- 停止位：1
- 校验：无

### 数据格式
ESP32 通过 UART1 向 STM32 发送纯文本字符串 + 换行符 `\n`。

### 消息列表
| 触发条件 | 发送内容 | 字节数 |
|---------|---------|--------|
| 手机连接 WiFi | `connect\n` | 9 字节 |
| 手机断开 WiFi | `discon\n` | 8 字节 |
| 手机连接后 | `http://192.168.4.1\n` | 21 字节 |

### 接收处理代码
```c
/* 检测换行符作为消息结束 */
if(byte == '\n' || byte == '\r') {
    /* 计算消息长度 */
    if(len > 0 && len < WIFI_FRAME_MAX_PAYLOAD) {
        /* 将消息放入队列 */
        memcpy(wifi_msg_payload[wifi_msg_head], &wifi_rx_buf[tail], len);
        wifi_msg_len[wifi_msg_head] = len;
        wifi_msg_head = (wifi_msg_head + 1) % WIFI_MSG_QUEUE_SIZE;
        wifi_msg_count++;
    }
}
```

### 消息解析
```c
/* 检查是否是 "connect" 消息 */
if(strstr(payload_str, "connect") != NULL) {
    wifi_connected = true;
    // 更新显示...
}

/* 检查是否包含 IP 地址 */
else if(strstr(payload_str, "http://") != NULL) {
    // 更新 IP 显示...
}
```

---

## 项目构建配置

### CMake 工具链文件
创建 `cmake/arm-none-eabi-gcc.cmake`：
```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX "C:/Users/.../arm-none-eabi-")
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_OBJCOPY "${TOOLCHAIN_PREFIX}objcopy.exe")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

### CMakeLists.txt 配置
添加 STM32H743 的 CPU/FPU 编译选项：
```cmake
# STM32H743 CPU flags (Cortex-M7 with FPv5-D16 FPU)
set(MCU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MCU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MCU_FLAGS} -T${CMAKE_CURRENT_SOURCE_DIR}/STM32H743XX_FLASH.ld")

# 生成 .bin 和 .hex 文件
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
)
```

### VS Code 配置文件

**.vscode/launch.json** (调试配置)
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "stlinkgdbtarget",
            "request": "launch",
            "name": "STM32Cube: Launch ST-Link GDB Server",
            "cwd": "${workspaceFolder}",
            "preBuild": "${command:st-stm32-ide-debug-launch.build}",
            "runEntry": "main",
            "imagesAndSymbols": [
                {
                    "imageFileName": "${command:st-stm32-ide-debug-launch.get-projects-binary-from-context1}"
                }
            ]
        }
    ]
}
```

**.vscode/tasks.json** (构建任务)
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "STM32 Build",
            "type": "shell",
            "command": "ninja",
            "args": ["-j4"],
            "options": { "cwd": "${workspaceFolder}/build" },
            "group": { "kind": "build", "isDefault": true },
            "problemMatcher": ["$gcc"]
        }
    ]
}
```

**.vscode/settings.json** (扩展设置)
```json
{
    "cmake.cmakePath": "cube-cmake",
    "cmake.preferredGenerators": ["Ninja"],
    "stm32cube-ide-clangd.path": "cube",
    "cortex-debug.openocdPath": ".../openocd.EXE",
    "stm32cube-ide-debug-launch.buildBeforeDebug": true,
    "stm32cube-ide-debug-launch.buildDirectory": "${workspaceFolder}/build"
}
```

### 构建命令
```bash
# 配置
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake

# 编译
cd build && ninja -j4

# 生成的文件
build/LVGL_project      # ELF 文件
build/LVGL_project.bin  # 二进制文件
build/LVGL_project.hex  # Intel HEX 文件
```

---

## 核心坑点13：看门狗功能

### 功能说明
在 SET 页面添加看门狗开关，防止程序跑飞。

### 实现要点

**1. 启用 IWDG 模块** (`stm32h7xx_hal_conf.h`)
```c
#define HAL_IWDG_MODULE_ENABLED
```

**2. 创建 IWDG 驱动文件**
- `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_iwdg.h`
- `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_iwdg.c`

**3. 添加到 CMakeLists.txt**
```cmake
${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_iwdg.c
```

**4. 看门狗初始化代码**
```c
static IWDG_HandleTypeDef hiwdg;

static void wdg_init(void)
{
    /* 超时时间约 1 秒
     * IWDG 时钟 = LSI / prescaler = 32kHz / 32 = 1kHz
     * 超时 = reload / 1kHz = 1000 / 1kHz = 1 秒
     */
    hiwdg.Instance = IWDG1;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 1000;
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    HAL_IWDG_Init(&hiwdg);
}
```

**5. 喂狗函数**
```c
static void wdg_refresh(void)
{
    if(wdg_enabled) {
        HAL_IWDG_Refresh(&hiwdg);
    }
}
```

**6. 在定时器中喂狗**
```c
static void data_update_timer_cb(lv_timer_t * timer)
{
    wdg_refresh();  /* 每 100ms 喂狗一次 */
    // ...
}
```

**7. SET 页面 UI**
```c
/* 看门狗开关 */
wdg_switch = lv_switch_create(scr_set);
lv_obj_set_size(wdg_switch, 50, 25);
lv_obj_set_pos(wdg_switch, 15, 150);
lv_obj_add_event_cb(wdg_switch, wdg_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

/* 看门狗状态标签 */
wdg_status_label = lv_label_create(scr_set);
lv_label_set_text(wdg_status_label, "Watchdog: OFF");
```

### 注意事项
- IWDG 一旦启用无法真正禁用，只能停止喂狗
- 喂狗间隔必须小于超时时间（1秒）
- 如果程序跑飞，看门狗会在超时后复位 MCU

---

## 核心坑点14：低功耗模式

### 功能说明
在 SET 页面添加低功耗模式开关，实现自动息屏和双击唤醒。

### 实现要点

**1. LCD 背光控制**
```c
/* 关闭 LCD 背光（息屏） */
static void lcd_backlight_off(void)
{
    HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin, GPIO_PIN_RESET);
    lp_screen_off = true;
}

/* 开启 LCD 背光（唤醒） */
static void lcd_backlight_on(void)
{
    HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin, GPIO_PIN_SET);
    lp_screen_off = false;
}
```

**2. 双击检测算法**
```c
static void lp_check_double_tap(int32_t raw_x, int32_t raw_y)
{
    uint32_t now = HAL_GetTick();

    if(pressed && !was_pressed) {
        /* 按下瞬间 */
        if(now - lp_last_touch_time < lp_tap_timeout) {
            /* 双击检测成功 */
            lp_tap_count++;
            if(lp_tap_count >= 2) {
                lcd_backlight_on();  /* 双击唤醒 */
                lp_tap_count = 0;
            }
        } else {
            lp_tap_count = 1;  /* 超时，重新计数 */
        }
        lp_last_touch_time = now;
    }
}
```

**3. 自动息屏检测**
```c
static void lp_auto_off_timer_cb(lv_timer_t * timer)
{
    if(!lp_enabled || lp_screen_off) return;

    /* 检测是否有触摸活动 */
    if(pressed) {
        last_activity_time = now;
    }

    /* 超过 30 秒无操作，自动息屏 */
    if(now - last_activity_time > 30000) {
        lcd_backlight_off();
    }
}
```

**4. SET 页面 UI**
```c
/* 低功耗模式开关 */
lp_switch = lv_switch_create(scr_set);
lv_obj_set_size(lp_switch, 50, 25);
lv_obj_set_pos(lp_switch, 15, 220);
lv_obj_add_event_cb(lp_switch, lp_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

/* 状态标签 */
lp_status_label = lv_label_create(scr_set);
lv_label_set_text(lp_status_label, "Low Power: OFF");
```

**5. 定时器配置**
```c
/* 自动息屏定时器（每 1 秒检查一次） */
lv_timer_create(lp_auto_off_timer_cb, 1000, NULL);
```

### 使用方法
1. 进入 SET 页面
2. 找到 "Low Power Mode" 区域
3. 滑动开关启用低功耗模式
4. 30 秒无操作自动息屏
5. 双击屏幕唤醒

### 注意事项
- 双击间隔需在 500ms 内
- 息屏后跳过所有触摸处理
- 禁用低功耗模式时会立即唤醒屏幕
- 自动息屏时间可在代码中调整（默认 30 秒）

---

## 总结

**核心原则**：
1. **触摸检测用 raw 坐标**，不要用 LVGL 坐标
2. **触摸范围必须实测**，不能靠想象
3. **添加调试信息**，方便定位问题
4. **注意内存和栈空间**，避免溢出
5. **使用绝对定位**，避免 flex 布局的复杂性
6. **USART1 需要手动添加中断处理函数和 NVIC 使能**
7. **使用消息队列避免快速发送时丢失消息**
8. **CubeMX 生成的代码需要检查 USER CODE 区域**