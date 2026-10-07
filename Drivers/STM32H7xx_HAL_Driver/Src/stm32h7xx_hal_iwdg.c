/**
  ******************************************************************************
  * @file    stm32h7xx_hal_iwdg.c
  * @brief   IWDG HAL module driver (minimal implementation for this project)
  ******************************************************************************
  */

#include "stm32h7xx_hal.h"

#ifdef HAL_IWDG_MODULE_ENABLED

/**
  * @brief  Initialize the IWDG according to the specified parameters
  *         in the IWDG_InitTypeDef and initialize the associated handle.
  * @param  hiwdg pointer to an IWDG_HandleTypeDef structure that contains
  *                the configuration information for the specified IWDG.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef *hiwdg)
{
  /* Check the IWDG handle allocation */
  if (hiwdg == NULL)
  {
    return HAL_ERROR;
  }

  /* Check the parameters */
  assert_param(IS_IWDG_ALL_INSTANCE(hiwdg->Instance));
  assert_param(IS_IWDG_PRESCALER(hiwdg->Init.Prescaler));
  assert_param(IS_IWDG_RELOAD(hiwdg->Init.Reload));
  assert_param(IS_IWDG_WINDOW(hiwdg->Init.Window));

  /* Enable IWDG. LSI is turned on automatically */
  hiwdg->Instance->KR = IWDG_KEY_ENABLE;

  /* Enable register access by writing Key */
  hiwdg->Instance->KR = IWDG_KEY_WRITE_ACCESS_ENABLE;

  /* Write to IWDG PR the Prescaler value */
  hiwdg->Instance->PR = hiwdg->Init.Prescaler;

  /* Write to IWDG RLR the Reload value */
  hiwdg->Instance->RLR = hiwdg->Init.Reload;

  /* Check the window option */
  if (hiwdg->Init.Window != IWDG_WINDOW_DISABLE)
  {
    /* Write to IWDG WINR the Window value */
    hiwdg->Instance->WINR = hiwdg->Init.Window;
  }

  return HAL_OK;
}

/**
  * @brief  Refresh the IWDG.
  * @param  hiwdg pointer to an IWDG_HandleTypeDef structure that contains
  *                the configuration information for the specified IWDG.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *hiwdg)
{
  /* Write to IWDG KR the IWDG_KEY_RELOAD value */
  hiwdg->Instance->KR = IWDG_KEY_RELOAD;

  return HAL_OK;
}

#endif /* HAL_IWDG_MODULE_ENABLED */