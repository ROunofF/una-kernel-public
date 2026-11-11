/**
 ******************************************************************************
 * @file    usbd_cdc_acm_if.c
 * @brief   USB CDC Interface.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_acm_if.h"
#include "assert.h"

#define USBD_CDC_RX_BUFF_SIZE   256
#define USBD_CDC_TX_BUFF_SIZE   256

extern USBD_CDC_ACM_HandleTypeDef CDC_ACM_Class_Data[NUMBER_OF_CDC];

/**
 * @brief CDC class component
 */
typedef struct {
    USBD_HandleTypeDef *pDev;
    USBD_CDC_PortOpenCb_t portCb;
    USBD_CDC_RxCb_t rxCb;

    uint8_t rxBuffer[NUMBER_OF_CDC][USBD_CDC_RX_BUFF_SIZE];
    uint8_t txBuffer[NUMBER_OF_CDC][USBD_CDC_TX_BUFF_SIZE];

    USBD_CDC_ACM_LineCodingTypeDef linecoding[NUMBER_OF_CDC];
    bool isPortOpen[NUMBER_OF_CDC];

    bool isConfigured;
} UsbdCdc_t;

static UsbdCdc_t Drv;

// Private function prototypes
static int8_t CDC_Init(uint8_t cdc_ch);
static int8_t CDC_DeInit(uint8_t cdc_ch);
static int8_t CDC_Control(uint8_t cdc_ch, uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive(uint8_t cdc_ch, uint8_t *pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len, uint8_t epnum);

void USBD_CDC_config(USBD_HandleTypeDef *pDev, USBD_CDC_PortOpenCb_t portCb,
        USBD_CDC_RxCb_t rxCb)
{
    assert(pDev != NULL);

    Drv.pDev = pDev;
    Drv.portCb = portCb;
    Drv.rxCb = rxCb;

    Drv.isConfigured = true;
}

USBD_CDC_ACM_ItfTypeDef USBD_CDC_getInterface()
{
    USBD_CDC_ACM_ItfTypeDef fops = { 0 };

    fops.Init = &CDC_Init;
    fops.DeInit = &CDC_DeInit;
    fops.Control = &CDC_Control;
    fops.Receive = &CDC_Receive;
    fops.TransmitCplt = &CDC_TransmitCplt;

    return fops;
}

bool USBD_CDC_Transmit(uint8_t ch, const uint8_t* pBuf, uint32_t len)
{
    if (Drv.isConfigured == false
            || Drv.isPortOpen[ch] == false
            || USBD_CDC_isTxBusy(ch))
    {
        return false;
    }

    USBD_CDC_SetTxBuffer(ch, Drv.pDev, (uint8_t*)pBuf, len);
    return USBD_CDC_TransmitPacket(ch, Drv.pDev) == USBD_OK;
}

bool USBD_CDC_isTxBusy(uint8_t ch)
{
    if (Drv.isConfigured == false) {
        return true;    // busy
    }

    USBD_CDC_ACM_HandleTypeDef *hcdc = &CDC_ACM_Class_Data[ch];

    if (hcdc == NULL) {
        return true;
    }

    return (hcdc[ch].TxState != 0);
}

bool USBD_CDC_isPortOpen(uint8_t ch)
{
    return Drv.isPortOpen[ch];
}

// USB lib Callbacks implementation
/**
 * @brief   Initializes the CDC media low layer over the FS USB IP
 * @retval  USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_Init(uint8_t cdc_ch)
{
    if (Drv.isConfigured == false) {
        return USBD_FAIL;
    }

    // Set Application Buffers
    USBD_CDC_SetTxBuffer(cdc_ch, Drv.pDev, Drv.txBuffer[cdc_ch], USBD_CDC_TX_BUFF_SIZE);
    USBD_CDC_SetRxBuffer(cdc_ch, Drv.pDev, Drv.rxBuffer[cdc_ch]);
    return USBD_OK;
}

/**
 * @brief   DeInitializes the CDC media low layer
 * @retval  USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_DeInit(uint8_t cdc_ch)
{
    if (Drv.isPortOpen[cdc_ch]) {
        Drv.isPortOpen[cdc_ch] = false;
        memset(&Drv.linecoding[cdc_ch], 0, sizeof(USBD_CDC_ACM_LineCodingTypeDef));
        if (Drv.portCb) {
            Drv.portCb(cdc_ch, false);
        }
    }
    return USBD_OK;
}

/**
 * @brief   Manage the CDC class requests
 * @param   cmd: Command code
 * @param   pbuf: Buffer containing command data (request parameters)
 * @param   length: Number of data to be sent (in bytes)
 * @retval  Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_Control(uint8_t cdc_ch, uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    switch (cmd) {
        case CDC_SEND_ENCAPSULATED_COMMAND:

            break;

        case CDC_GET_ENCAPSULATED_RESPONSE:

            break;

        case CDC_SET_COMM_FEATURE:

            break;

        case CDC_GET_COMM_FEATURE:

            break;

        case CDC_CLEAR_COMM_FEATURE:

            break;

        /*******************************************************************************/
        /* Line Coding Structure                                                       */
        /*-----------------------------------------------------------------------------*/
        /* Offset | Field       | Size | Value  | Description                          */
        /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
        /* 4      | bCharFormat |   1  | Number | Stop bits                            */
        /*                                        0 - 1 Stop bit                       */
        /*                                        1 - 1.5 Stop bits                    */
        /*                                        2 - 2 Stop bits                      */
        /* 5      | bParityType |  1   | Number | Parity                               */
        /*                                        0 - None                             */
        /*                                        1 - Odd                              */
        /*                                        2 - Even                             */
        /*                                        3 - Mark                             */
        /*                                        4 - Space                            */
        /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
        /*******************************************************************************/
        case CDC_SET_LINE_CODING:
            Drv.linecoding[cdc_ch].bitrate = (uint32_t) (pbuf[0]
                                                      | (pbuf[1] << 8)
                                                      | (pbuf[2] << 16)
                                                      | (pbuf[3] << 24));
            Drv.linecoding[cdc_ch].format = pbuf[4];
            Drv.linecoding[cdc_ch].paritytype = pbuf[5];
            Drv.linecoding[cdc_ch].datatype = pbuf[6];

            if (Drv.linecoding[cdc_ch].bitrate != 0
                    && Drv.linecoding[cdc_ch].datatype != 0)
            {
                if (!Drv.isPortOpen[cdc_ch]) {
                    if (Drv.portCb) {
                        Drv.portCb(cdc_ch, true);
                    }
                }
                Drv.isPortOpen[cdc_ch] = true;
            }
            break;

        case CDC_GET_LINE_CODING:
            pbuf[0] = (uint8_t) (Drv.linecoding[cdc_ch].bitrate);
            pbuf[1] = (uint8_t) (Drv.linecoding[cdc_ch].bitrate >> 8);
            pbuf[2] = (uint8_t) (Drv.linecoding[cdc_ch].bitrate >> 16);
            pbuf[3] = (uint8_t) (Drv.linecoding[cdc_ch].bitrate >> 24);
            pbuf[4] = Drv.linecoding[cdc_ch].format;
            pbuf[5] = Drv.linecoding[cdc_ch].paritytype;
            pbuf[6] = Drv.linecoding[cdc_ch].datatype;
            break;

        case CDC_SET_CONTROL_LINE_STATE:
            USBD_SetupReqTypedef *req = (USBD_SetupReqTypedef*) pbuf;
            bool isOpen = ((req->wValue & 0x0001) != 0);

            if (Drv.isPortOpen[cdc_ch] && !isOpen) {
                // Clear local resources
                CDC_DeInit(cdc_ch);
            }
            break;

        case CDC_SEND_BREAK:

            break;

        default:
            break;
    }

    return (USBD_OK);

}

/**
 * @brief   Data received over USB OUT endpoint are sent over CDC interface
 *          through this function.
 * @note    This function will issue a NAK packet on any OUT packet received on
 *          USB endpoint until exiting this function. If you exit this function
 *          before transfer is complete on CDC interface (ie. using DMA controller)
 *          it will result in receiving more data while previous ones are still
 *          not sent.
 * @param   Buf: Buffer of data to be received
 * @param   Len: Number of data received (in bytes)
 * @retval  Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_Receive(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len)
{
    if (Drv.isConfigured == false) {
        return USBD_FAIL;
    }

    if (Drv.rxCb) {
        Drv.rxCb(cdc_ch, Buf, *Len);
    }

    if (USBD_CDC_SetRxBuffer(cdc_ch, Drv.pDev, &Buf[0]) != USBD_OK) {
        return USBD_FAIL;
    }

    if (USBD_CDC_ReceivePacket(cdc_ch, Drv.pDev) != USBD_OK) {
        return USBD_FAIL;
    }

    return USBD_OK;
}

/**
 * @brief   Data transmited callback
 * @note    This function is IN transfer complete callback used to inform user that
 *          the submitted Data is successfully sent over USB.
 * @param   Buf: Buffer of data to be received
 * @param   Len: Number of data received (in bytes)
 * @retval  Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
 */
static int8_t CDC_TransmitCplt(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len,
        uint8_t epnum)
{
    return (USBD_OK);
}
