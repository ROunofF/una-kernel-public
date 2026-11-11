/**
 ******************************************************************************
 * @file    usbd_conf.c
 * @brief   This file implements the board support package for the USB device
 *          library
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

#include "main.h"

#include "Usb.h"

#include "usbd_def.h"
#include "usbd_core.h"
#include "usbd_composite.h"
#include "assert.h"



// Private function prototypes
static USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status);

#if ((USBD_USE_CDC_ACM == 1) && (USBD_USE_MSC == 1))
static_assert( sizeof(USBD_CDC_ACM_HandleTypeDef) != sizeof(USBD_MSC_BOT_HandleTypeDef) );
#endif

HAL_StatusTypeDef HAL_PCDEx_SetTxFiFoInBytes(PCD_HandleTypeDef *hpcd,
        uint8_t fifo, uint16_t size)
{
    return HAL_PCDEx_SetTxFiFo(hpcd, fifo, (size / 4));
}

HAL_StatusTypeDef HAL_PCDEx_SetRxFiFoInBytes(PCD_HandleTypeDef *hpcd,
        uint16_t size)
{
    return HAL_PCDEx_SetRxFiFo(hpcd, (size / 4));
}

// LL Driver Callbacks (PCD -> USB Device Library)

/**
 * @brief   Setup stage callback
 * @param   hpcd: PCD handle
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_SetupStage((USBD_HandleTypeDef*) hpcd->pData,
            (uint8_t*) hpcd->Setup);
}

/**
 * @brief   Data Out stage callback.
 * @param   hpcd: PCD handle
 * @param   epnum: Endpoint number
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_DataOutStage((USBD_HandleTypeDef*) hpcd->pData, epnum,
            hpcd->OUT_ep[epnum].xfer_buff);
}

/**
 * @brief   Data In stage callback.
 * @param   hpcd: PCD handle
 * @param   epnum: Endpoint number
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_DataInStage((USBD_HandleTypeDef*) hpcd->pData, epnum,
            hpcd->IN_ep[epnum].xfer_buff);
}

/**
 * @brief   SOF callback.
 * @param   hpcd: PCD handle
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_SOF((USBD_HandleTypeDef*) hpcd->pData);
}

/**
 * @brief  Reset callback.
 * @param  hpcd: PCD handle
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_SpeedTypeDef speed = USBD_SPEED_FULL;

    if (hpcd->Init.speed == PCD_SPEED_FULL) {
        speed = USBD_SPEED_FULL;
    } else if (hpcd->Init.speed == PCD_SPEED_HIGH) {
        speed = USBD_SPEED_HIGH;
    } else {
        Error_Handler();
    }
    // Set Speed
    USBD_LL_SetSpeed((USBD_HandleTypeDef*) hpcd->pData, speed);

    // Reset Device
    USBD_LL_Reset((USBD_HandleTypeDef*) hpcd->pData);
}

/**
 * @brief   Suspend callback.
 *          When Low power mode is enabled the debug cannot be used
 * @param   hpcd: PCD handle
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    // Inform USB library that core enters in suspend Mode.
    USBD_LL_Suspend((USBD_HandleTypeDef*) hpcd->pData);

    __HAL_PCD_GATE_PHYCLOCK(hpcd);
    // Enter in STOP mode.

    if (hpcd->Init.low_power_enable) {
        // Set SLEEPDEEP bit and SleepOnExit of Cortex System Control Register.
        SCB->SCR |= (uint32_t) ((uint32_t) (SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk));
    }
}

/**
 * @brief   Resume callback.
 *          When Low power mode is enabled the debug cannot be used.
 * @param   hpcd: PCD handle
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_Resume((USBD_HandleTypeDef*) hpcd->pData);
}

/**
 * @brief   ISOOUTIncomplete callback.
 * @param   hpcd: PCD handle
 * @param   epnum: Endpoint number
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef*) hpcd->pData, epnum);
}

/**
 * @brief   ISOINIncomplete callback.
 * @param   hpcd: PCD handle
 * @param   epnum: Endpoint number
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*) hpcd->pData, epnum);
}

/**
 * @brief   Connect callback.
 * @param   hpcd: PCD handle
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_DevConnected((USBD_HandleTypeDef*) hpcd->pData);
}

/**
 * @brief  Disconnect callback.
 * @param  hpcd: PCD handle
 */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    USBD_LL_DevDisconnected((USBD_HandleTypeDef*) hpcd->pData);
}

// LL Driver Interface (USB Device Library --> PCD)

/**
 * @brief   Initializes the low level portion of the device driver.
 * @param   pdev: Device handle
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    PCD_HandleTypeDef *pcdHandle = DriverUsb_GetHandle();

    // Link the driver to the stack.
    pcdHandle->pData = pdev;
    pdev->pData = pcdHandle;

    bool isHS = (DriverUsb_Speed_t)pdev->id == DRIVER_USB_HS;
    if (DriverUsb_PCD_Init((DriverUsb_Speed_t)pdev->id) != HAL_OK) {
        return USBD_FAIL;
    }

    // ALL OUT EP Buffer. For HS 512+
    uint16_t rxFifoSize = isHS ? 1024 : 512;

    // EP0 IN min 512 for HS
    uint16_t txFifoEp0Size = 64;

    HAL_PCDEx_SetRxFiFoInBytes(pcdHandle, rxFifoSize);
    HAL_PCDEx_SetTxFiFoInBytes(pcdHandle, 0, txFifoEp0Size);

#if (USBD_USE_MSC == 1)
    // MSC TX
    uint16_t txFifoMscSize = isHS ? 1024 : 128;
    HAL_PCDEx_SetTxFiFoInBytes(pcdHandle, (MSC_IN_EP & 0x7F), txFifoMscSize);
#endif
#if (USBD_USE_CDC_ACM == 1)
    for (uint8_t i = 0; i < USBD_CDC_ACM_COUNT; i++) {
        HAL_PCDEx_SetTxFiFoInBytes(pcdHandle, (CDC_IN_EP[i] & 0x7F), USB_CDC_ACM_TX_FIFO);
        HAL_PCDEx_SetTxFiFoInBytes(pcdHandle, (CDC_CMD_EP[i] & 0x7F), 64);
    }
#endif


#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
  // Register USB PCD CallBacks
  HAL_PCD_RegisterCallback(pcdHandle, HAL_PCD_SOF_CB_ID, PCD_SOFCallback);
  HAL_PCD_RegisterCallback(pcdHandle, HAL_PCD_SETUPSTAGE_CB_ID, PCD_SetupStageCallback);
  HAL_PCD_RegisterCallback(pcdHandle, HAL_PCD_RESET_CB_ID, PCD_ResetCallback);
  HAL_PCD_RegisterCallback(pcdHandle, HAL_PCD_SUSPEND_CB_ID, PCD_SuspendCallback);
  HAL_PCD_RegisterCallback(pcdHandle, HAL_PCD_RESUME_CB_ID, PCD_ResumeCallback);
  HAL_PCD_RegisterCallback(pcdHandle, HAL_PCD_CONNECT_CB_ID, PCD_ConnectCallback);
  HAL_PCD_RegisterCallback(pcdHandle, HAL_PCD_DISCONNECT_CB_ID, PCD_DisconnectCallback);

  HAL_PCD_RegisterDataOutStageCallback(pcdHandle, PCD_DataOutStageCallback);
  HAL_PCD_RegisterDataInStageCallback(pcdHandle, PCD_DataInStageCallback);
  HAL_PCD_RegisterIsoOutIncpltCallback(pcdHandle, PCD_ISOOUTIncompleteCallback);
  HAL_PCD_RegisterIsoInIncpltCallback(pcdHandle, PCD_ISOINIncompleteCallback);
#endif // USE_HAL_PCD_REGISTER_CALLBACKS

  return USBD_OK;
}

/**
 * @brief   De-Initializes the low level portion of the device driver.
 * @param   pdev: Device handle
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = DriverUsb_PCD_DeInit(pdev->pData);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Starts the low level portion of the device driver.
 * @param   pdev: Device handle
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_Start(pdev->pData);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Stops the low level portion of the device driver.
 * @param   pdev: Device handle
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_Stop(pdev->pData);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Opens an endpoint of the low level driver.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @param   ep_type: Endpoint type
 * @param   ep_mps: Endpoint max packet size
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
        uint8_t ep_type, uint16_t ep_mps)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Closes an endpoint of the low level driver.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_EP_Close(pdev->pData, ep_addr);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Flushes an endpoint of the Low Level Driver.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_EP_Flush(pdev->pData, ep_addr);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Sets a Stall condition on an endpoint of the Low Level Driver.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_EP_SetStall(pdev->pData, ep_addr);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Clears a Stall condition on an endpoint of the Low Level Driver.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev,
        uint8_t ep_addr)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Returns Stall condition.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @retval  Stall (1: Yes, 0: No)
 */
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef*) pdev->pData;

    if ((ep_addr & 0x80) == 0x80) {
        return hpcd->IN_ep[ep_addr & 0x7F].is_stall;
    } else {
        return hpcd->OUT_ep[ep_addr & 0x7F].is_stall;
    }
}

/**
 * @brief   Assigns a USB address to the device.
 * @param   pdev: Device handle
 * @param   dev_addr: Device address
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev,
        uint8_t dev_addr)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_SetAddress(pdev->pData, dev_addr);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Transmits data over an endpoint.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @param   pbuf: Pointer to data to be sent
 * @param   size: Data size
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
        uint8_t *pbuf, uint32_t size)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Prepares an endpoint for reception.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @param   pbuf: Pointer to data to be received
 * @param   size: Data size
 * @retval  USBD status
 */
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,
        uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBD_StatusTypeDef usb_status = USBD_OK;

    hal_status = HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);

    usb_status = USBD_Get_USB_Status(hal_status);

    return usb_status;
}

/**
 * @brief   Returns the last transferred packet size.
 * @param   pdev: Device handle
 * @param   ep_addr: Endpoint number
 * @retval  Received Data Size
 */
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef*) pdev->pData, ep_addr);
}


/**
 * @brief  Static single allocation.
 * @param  size: Size of allocated memory
 * @retval Pointer to the allocated memory.
 */
void* USBD_static_malloc(uint32_t size)
{

#if (USBD_USE_MSC == 1)
    if (size == sizeof(USBD_MSC_BOT_HandleTypeDef)) {
        static uint32_t mem[(sizeof(USBD_MSC_BOT_HandleTypeDef) / 4) + 1];/* On 32-bit boundary */
        return mem;
    }
#endif //  (USBD_USE_MSC == 1)

#if (USBD_USE_CDC_ACM == 1)
    if (size == sizeof(USBD_CDC_ACM_HandleTypeDef)) {
        static uint32_t mem[(sizeof(USBD_CDC_ACM_HandleTypeDef) / 4) + 1];/* On 32-bit boundary */
        return mem;
    }
#endif // (USBD_USE_CDC_ACM == 1)

    return NULL;
}

/**
 * @brief  Dummy memory free
 * @param  p: Pointer to allocated  memory address
 */
void USBD_static_free(void *p)
{
    UNUSED(p);
}

/**
 * @brief   Delays routine for the USB device library.
 * @param   Delay: Delay in ms
 */
void USBD_LL_Delay(uint32_t Delay)
{
    DriverUsb_Delay(Delay);
}

/**
 * @brief   Returns the USB status depending on the HAL status:
 * @param   hal_status: HAL status
 * @retval  USB status
 */
USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status)
{
    USBD_StatusTypeDef usb_status = USBD_OK;

    switch (hal_status) {
        case HAL_OK:
            usb_status = USBD_OK;
            break;
        case HAL_ERROR:
            usb_status = USBD_FAIL;
            break;
        case HAL_BUSY:
            usb_status = USBD_BUSY;
            break;
        case HAL_TIMEOUT:
            usb_status = USBD_FAIL;
            break;
        default:
            usb_status = USBD_FAIL;
            break;
    }
    return usb_status;
}
