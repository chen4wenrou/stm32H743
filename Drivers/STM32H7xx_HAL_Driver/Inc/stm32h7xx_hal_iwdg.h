/**
  ******************************************************************************
  * @file    stm32h7xx_hal_iwdg.h
  * @brief   Header file of IWDG HAL module (minimal implementation)
  ******************************************************************************
  */

#ifndef STM32H7xx_HAL_IWDG_H
#define STM32H7xx_HAL_IWDG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal_def.h"

/** @defgroup IWDG_Exported_Defines IWDG Exported Defines
  * @{
  */

/* IWDG Key Register */
#define IWDG_KEY_RELOAD                0x0000AAAAU  /* IWDG Reload Counter Enable */
#define IWDG_KEY_ENABLE                0x0000CCCCU  /* IWDG Peripheral Enable */
#define IWDG_KEY_WRITE_ACCESS_ENABLE   0x00005555U  /* IWDG KR Write Access Enable */
#define IWDG_KEY_WRITE_ACCESS_DISABLE  0x00000000U  /* IWDG KR Write Access Disable */

/** @defgroup IWDG_Prescaler IWDG Prescaler
  * @{
  */
#define IWDG_PRESCALER_4   0x00000000U  /* IWDG prescaler set to 4 */
#define IWDG_PRESCALER_8   IWDG_PR_PR_0  /* IWDG prescaler set to 8 */
#define IWDG_PRESCALER_16  IWDG_PR_PR_1  /* IWDG prescaler set to 16 */
#define IWDG_PRESCALER_32  (IWDG_PR_PR_1 | IWDG_PR_PR_0)  /* IWDG prescaler set to 32 */
#define IWDG_PRESCALER_64  IWDG_PR_PR_2  /* IWDG prescaler set to 64 */
#define IWDG_PRESCALER_128 (IWDG_PR_PR_2 | IWDG_PR_PR_0)  /* IWDG prescaler set to 128 */
#define IWDG_PRESCALER_256 (IWDG_PR_PR_2 | IWDG_PR_PR_1 | IWDG_PR_PR_0)  /* IWDG prescaler set to 256 */

/** @defgroup IWDG_Window IWDG Window
  * @{
  */
#define IWDG_WINDOW_DISABLE  0x00000FFFU  /* IWDG Window Disable */

/**
  * @brief  IWDG Init structure definition
  */
typedef struct
{
  uint32_t Prescaler;  /* Select the prescaler of the IWDG */
  uint32_t Reload;     /* Specifies the IWDG down-counter reload value */
  uint32_t Window;     /* Specifies the window value to be compared to the down-counter */
} IWDG_InitTypeDef;

/**
  * @brief  IWDG Handle Structure definition
  */
typedef struct
{
  IWDG_TypeDef         *Instance;  /* Register base address */
  IWDG_InitTypeDef     Init;       /* IWDG required parameters */
} IWDG_HandleTypeDef;

/**
  * @brief  Initialize the IWDG according to the specified parameters
  */
HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef *hiwdg);

/**
  * @brief  Refresh the IWDG.
  */
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *hiwdg);

#ifdef __cplusplus
}
#endif

#endif /* STM32H7xx_HAL_IWDG_H */