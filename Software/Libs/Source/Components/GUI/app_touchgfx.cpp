/**
  ******************************************************************************
  * File Name          : app_touchgfx.cpp
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

/* Includes ------------------------------------------------------------------*/
#include "app_touchgfx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "crc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/

/* USER CODE BEGIN PD */
#include <touchgfx/hal/OSWrappers.hpp>
#include "OS.hpp"
#include "FramePacer.hpp"

// Every frame handed out is logged at debug, which is a line or two a second on
// a screen that is doing nothing. Raise this to LOG_LEVEL_DEBUG to watch the
// pacing; at info the calls compile away and the lines stay for the next time.
#define LOG_MODULE_PRX      "app_touchgfx"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
#define skGuiThreadStackSizeW (4*1024)
static osThreadId_t taskHandle;
static uint32_t taskBuffer[skGuiThreadStackSizeW];
static StaticTask_t taskControlBlock;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
extern "C" void touchgfx_init(void);
extern "C" void touchgfx_components_init(void);
extern "C" void touchgfx_taskEntry(void);

/* USER CODE BEGIN PFP */
// Generate frame update events

// How many frames and when is FramePacer's; this is the three things it asks
// for, and the timer they are asked of.
static OS::TimerStatic sFrameTimer;

static const uint32_t skFramePeriodMs = 1000 / 10;

/// Frames a run hands out.
static const uint32_t skRunFrames = 10;

struct FrameEffects {
    void signalFrame() { touchgfx::OSWrappers::signalVSync(); }
    void startTimer()  { sFrameTimer.start(skFramePeriodMs); }
    void stopTimer()   { sFrameTimer.stop(); }
};

static FrameEffects sFrameEffects;
static FramePacer<FrameEffects> sPacer(sFrameEffects, skRunFrames);

static void FrameSync(void *arg)
{
    if (sPacer.onPass()) {
        LOG_DEBUG("Frames done\n");
    }
}

/* USER CODE END PFP */

/**
 * PreOS Initialization function
 */
void MX_TouchGFX_PreOSInit(void)
{
}

/**
 * Initialize TouchGFX application
 */
void MX_TouchGFX_Init(void)
{
    // Required for TouchGFX
    MX_CRC_Init();

    // Calling forward to touchgfx_init in C++ domain
    touchgfx_components_init();
    touchgfx_init();

    sFrameTimer.init("FrameTimer", FrameSync, true);
    sFrameTimer.start(skFramePeriodMs);

    // Creation of TouchGFXTask
    const osThreadAttr_t kEvtTaskAttributes = {
           .name = "TouchGFX",
           .cb_mem = &taskControlBlock,
           .cb_size = sizeof(taskControlBlock),
           .stack_mem = &taskBuffer[0],
           .stack_size = sizeof(taskBuffer),
           .priority = (osPriority_t) osPriorityNormal
    };
    taskHandle = osThreadNew(TouchGFX_Task, NULL, &kEvtTaskAttributes);

}

/**
 * TouchGFX application entry function
 */
void MX_TouchGFX_Process(void)
{
    // Calling forward to touchgfx_taskEntry in C++ domain
    touchgfx_taskEntry();
}

/**
 * TouchGFX application thread
 */
void TouchGFX_Task(void* argument)
{
    // Calling forward to touchgfx_taskEntry in C++ domain
    touchgfx_taskEntry();
}

void TouchGFX_EnableLowFrameRate(bool enable)
{
    if (sPacer.setLowRate(enable)) {
        LOG_DEBUG("Frame beat %s\n", enable ? "stopped" : "started");
    }
}

void TouchGFX_ForceFrame()
{
    if (sPacer.forceRun()) {
        LOG_DEBUG("Frames owed: %u\n", static_cast<unsigned>(skRunFrames));
    }
}

void TouchGFX_RequestFrame()
{
    if (sPacer.requestOne()) {
        LOG_DEBUG("Frame owed\n");
    }
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
