/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lvgl.h"
#include "ui.h"
#include "lv_port_lcd_stm32.h"
#include "lv_port_indev.h"
#include "adc.h"              // ADC 外设头文件
#include "tim.h"              // TIM2 PWM 外设头文件
#include "usart.h"            // UART4 外设头文件
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* ★ FreeRTOS 堆内存 - 放在 RAM_D2 (288KB) 中
 * 使用 __attribute__((section())) 将数组放到 RAM_D2 区域
 * 这样 RAM (512KB) 可以全部给 LVGL 使用 */
__attribute__((section(".freertos_heap")))
uint8_t ucHeap[configTOTAL_HEAP_SIZE];

/* ★ 共享变量：ADC 任务写入，LVGL 定时器读取
 * 用 volatile 告诉编译器："这个变量随时可能被别的任务修改，
 * 每次用它都去内存里重新读，不要用缓存的旧值"
 *
 * 5 个通道对应 5 个 Rank：
 *   [0] = Channel 2  (PF11)    Rank 1
 *   [1] = Channel 10 (PC0)     Rank 2
 *   [2] = Channel 11 (PC1)     Rank 3
 *   [3] = Channel 14 (PA2)     Rank 4
 *   [4] = Channel 15 (PA3)     Rank 5 */
volatile uint32_t g_adc_raw[5] = {0};   // 5 个通道的原始 ADC 值
osThreadId adcTaskHandle;               // ADC 任务句柄
/* USER CODE END Variables */
osThreadId defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void StartAdcTask(void const * argument);   // ← 新增：ADC 任务函数声明
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 2048);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* ★ 创建 ADC 任务
   * 参数说明：
   *   adcTask         → 任务名字
   *   StartAdcTask    → 任务函数
   *   osPriorityBelowNormal → 优先级比 LVGL 低，不会抢 LVGL 的 CPU 时间
   *   0               → 不用实例参数
   *   1024            → 栈大小 1024 words = 4KB，够用了 */
  osThreadDef(adcTask, StartAdcTask, osPriorityBelowNormal, 0, 1024);
  adcTaskHandle = osThreadCreate(osThread(adcTask), NULL);
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */

  /* 1. 初始化 LVGL 核心 */
  lv_init();

  /* 2. 心跳：告诉 LVGL 用 HAL 的毫秒计数来计时 */
  lv_tick_set_cb(HAL_GetTick);

  /* 3. 初始化屏幕驱动（ST7789 + SPI + DMA） */
  lv_port_display_init();

  /* 4. 初始化你的 UI */
  ui_init();

  /* 5. 初始化触摸输入设备（FT6206），让屏幕能响应手指 */
  lv_port_indev_init();

  /* 6. 启动 PWM 输出（PA0 = TIM2_CH1, PA1 = TIM2_CH2, PB10 = TIM2_CH3） */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);

  /* 7. 主循环：反复推进 LVGL 状态机 */
  for(;;)
  {
    lv_timer_handler();   // LVGL 的"发动机"，每次调用处理刷新/动画/事件
    osDelay(5);           // 让出 CPU，5ms 调度一次
  }

  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
 * ★ ADC 任务：每隔 200ms 读一次 ADC，把结果存到全局变量
 *
 * 整个流程只有 3 步：
 *   1. 启动 ADC（只做一次）
 *   2. 触发一次转换
 *   3. 等转换完成，读取结果
 *
 * 为什么不读完直接更新屏幕？
 *   因为 LVGL 控件不是线程安全的——两个任务不能同时操作 LVGL。
 *   所以 ADC 任务只负责"读"，把值存到全局变量；
 *   屏幕更新由 LVGL 任务自己的定时器负责"读取变量 + 显示"。
 */
void StartAdcTask(void const * argument)
{
    (void)argument;

    /* --- 初始化：只执行一次 --- */
    HAL_ADC_Start(&hadc1);    // 启动 ADC，让它准备好

    /* ★ 5 个通道的信息表（Channel 号 和 对应的 Rank）
     * 每次只转换 1 个通道，读完换下一个，5 个轮流来 */
    static const struct { uint32_t channel; uint32_t rank; } adc_ch[5] = {
        { ADC_CHANNEL_2,  ADC_REGULAR_RANK_1 },   // [0] PF11
        { ADC_CHANNEL_10, ADC_REGULAR_RANK_2 },   // [1] PC0
        { ADC_CHANNEL_11, ADC_REGULAR_RANK_3 },   // [2] PC1
        { ADC_CHANNEL_14, ADC_REGULAR_RANK_4 },   // [3] PA2
        { ADC_CHANNEL_15, ADC_REGULAR_RANK_5 },   // [4] PA3
    };

    /* --- 主循环：反复执行 --- */
    for(;;)
    {
        /* 轮流读取 5 个通道：每次只转换 1 个，读完换下一个 */
        for(int i = 0; i < 5; i++)
        {
            /* 第 1 步：配置当前要读的通道 */
            ADC_ChannelConfTypeDef sConfig = {0};
            sConfig.Channel      = adc_ch[i].channel;
            sConfig.Rank         = adc_ch[i].rank;
            sConfig.SamplingTime = ADC_SAMPLETIME_8CYCLES_5;
            sConfig.SingleDiff   = ADC_SINGLE_ENDED;
            HAL_ADC_ConfigChannel(&hadc1, &sConfig);

            /* 第 2 步：启动转换，等它完成 */
            HAL_ADC_Start(&hadc1);
            HAL_ADC_PollForConversion(&hadc1, 100);

            /* 第 3 步：读取结果 */
            g_adc_raw[i] = HAL_ADC_GetValue(&hadc1);

            /* 第 4 步：停止 ADC（为下一次切换通道做准备） */
            HAL_ADC_Stop(&hadc1);
        }

        /* 没接传感器的通道，会读到随机值（浮空引脚噪声），这是正常的 */

        osDelay(200);   // 每 200ms 读一轮，让出 CPU 给其他任务
    }
}

/* USER CODE END Application */
