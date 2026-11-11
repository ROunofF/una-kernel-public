/**
  ******************************************************************************
  * File Name          : app_touchgfx.h
  ******************************************************************************
  * This file was created by TouchGFX Generator 4.23.0. This file is only
  * generated once! Delete this file from your project and re-generate code
  * using STM32CubeMX or change this file manually to update it.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef APP_TOUCHGFX_H
#define APP_TOUCHGFX_H
#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void MX_TouchGFX_PreOSInit(void);
void MX_TouchGFX_Init(void);
void MX_TouchGFX_Process(void);
void TouchGFX_Task(void* argument);

/* USER CODE BEGIN EFP */
void TouchGFX_EnableLowFrameRate(bool enable);
/**
 * @brief Let the GUI run on for a while, for work whose extent is not knowable
 *        from here -- a screen change, an animation, several events at once.
 */
void TouchGFX_ForceFrame();

/**
 * @brief Ask for one frame, for work whose extent is exactly one -- an app
 *        frame waiting to be carried to the panel.
 */
void TouchGFX_RequestFrame();

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif
#endif /* APP_TOUCHGFX_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
