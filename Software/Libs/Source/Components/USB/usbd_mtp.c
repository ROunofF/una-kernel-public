/**
 ******************************************************************************
 * @file    usbd_mtp.c
 * @brief   MTP interface for the AL94 composite: descriptor, endpoints and
 *          the PTP class requests. No file work happens here; see usbd_mtp.h.
 ******************************************************************************
 */

#include "usbd_mtp.h"
#include "usbd_ctlreq.h"
#include "main.h"
#include <string.h>

/* PTP / Still Image class requests (PIMA 15740 USB transport, 5.2) */
#define MTP_REQ_CANCEL                  0x64U
#define MTP_REQ_GET_EXT_EVENT_DATA      0x65U
#define MTP_REQ_DEVICE_RESET            0x66U
#define MTP_REQ_GET_DEVICE_STATUS       0x67U

#define MTP_RESP_OK                     0x2001U
#define MTP_RESP_DEVICE_BUSY            0x2019U

uint8_t MTP_IN_EP        = 0x81U;
uint8_t MTP_OUT_EP       = 0x01U;
uint8_t MTP_INT_EP       = 0x82U;
uint8_t MTP_ITF_NBR      = 0x00U;
uint8_t MTP_STR_DESC_IDX = 0x00U;

/* For the `usb` CLI command: control requests sent to this interface. */
volatile uint32_t USBD_MTP_SetupCount;
volatile uint32_t USBD_MTP_LastSetup;   /* bmRequest << 8 | bRequest */

static USBD_HandleTypeDef        *sDev;
static const USBD_MTP_Callbacks  *sCb;
static volatile bool              sOpen;
static volatile bool              sBusy;
static uint8_t                    sInterface;
static uint8_t                    sCancelPending;

__ALIGN_BEGIN static uint8_t sEp0Buf[8] __ALIGN_END;

static uint8_t USBD_MTP_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_MTP_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_MTP_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_MTP_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_MTP_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_MTP_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t *USBD_MTP_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_MTP_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_MTP_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_MTP_GetDeviceQualifierDesc(uint16_t *length);

USBD_ClassTypeDef USBD_MTP =
{
    USBD_MTP_Init,
    USBD_MTP_DeInit,
    USBD_MTP_Setup,
    NULL,                   /* EP0_TxSent */
    USBD_MTP_EP0_RxReady,
    USBD_MTP_DataIn,
    USBD_MTP_DataOut,
    NULL,                   /* SOF */
    NULL,
    NULL,
    USBD_MTP_GetHSCfgDesc,
    USBD_MTP_GetFSCfgDesc,
    USBD_MTP_GetOtherSpeedCfgDesc,
    USBD_MTP_GetDeviceQualifierDesc,
};

#define MTP_CFG_DESC(mps, interval)                                            \
    {                                                                          \
        /* Configuration descriptor (dropped by the composite) */             \
        0x09, USB_DESC_TYPE_CONFIGURATION,                                     \
        LOBYTE(USB_MTP_CONFIG_DESC_SIZ), HIBYTE(USB_MTP_CONFIG_DESC_SIZ),      \
        0x01, 0x01, 0x00, 0xC0, 0x32,                                          \
        /* Interface: Still Image / PTP, which is how hosts find MTP */       \
        0x09, USB_DESC_TYPE_INTERFACE,                                         \
        0x00,                   /* bInterfaceNumber (patched) */               \
        0x00, 0x03,             /* bAlternateSetting, bNumEndpoints */         \
        0x06, 0x01, 0x01,       /* Still Image, Capture, PTP */                \
        0x00,                   /* iInterface (patched) */                     \
        /* Bulk IN */                                                          \
        0x07, USB_DESC_TYPE_ENDPOINT, 0x81, 0x02,                              \
        LOBYTE(mps), HIBYTE(mps), 0x00,                                        \
        /* Bulk OUT */                                                         \
        0x07, USB_DESC_TYPE_ENDPOINT, 0x01, 0x02,                              \
        LOBYTE(mps), HIBYTE(mps), 0x00,                                        \
        /* Interrupt IN (events) */                                            \
        0x07, USB_DESC_TYPE_ENDPOINT, 0x82, 0x03,                              \
        LOBYTE(MTP_EVENT_PACKET), HIBYTE(MTP_EVENT_PACKET), (interval),        \
    }

__ALIGN_BEGIN static uint8_t USBD_MTP_CfgHSDesc[USB_MTP_CONFIG_DESC_SIZ] __ALIGN_END =
    MTP_CFG_DESC(MTP_MAX_HS_PACKET, 0x08);      /* 2^(8-1) x 125 us = 16 ms */

__ALIGN_BEGIN static uint8_t USBD_MTP_CfgFSDesc[USB_MTP_CONFIG_DESC_SIZ] __ALIGN_END =
    MTP_CFG_DESC(MTP_MAX_FS_PACKET, 0x10);      /* 16 ms */

__ALIGN_BEGIN static uint8_t USBD_MTP_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
    USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

/* -------------------------------------------------------------------------- */

void USBD_MTP_SetCallbacks(const USBD_MTP_Callbacks *cb)
{
    sCb = cb;
}

void USBD_MTP_SetBusy(bool busy)
{
    sBusy = busy;
}

static uint8_t USBD_MTP_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);
    const uint16_t mps = (pdev->dev_speed == USBD_SPEED_HIGH)
                       ? MTP_MAX_HS_PACKET : MTP_MAX_FS_PACKET;

    sDev = pdev;

    (void)USBD_LL_OpenEP(pdev, MTP_IN_EP, USBD_EP_TYPE_BULK, mps);
    pdev->ep_in[MTP_IN_EP & 0xFU].is_used = 1U;

    (void)USBD_LL_OpenEP(pdev, MTP_OUT_EP, USBD_EP_TYPE_BULK, mps);
    pdev->ep_out[MTP_OUT_EP & 0xFU].is_used = 1U;

    (void)USBD_LL_OpenEP(pdev, MTP_INT_EP, USBD_EP_TYPE_INTR, MTP_EVENT_PACKET);
    pdev->ep_in[MTP_INT_EP & 0xFU].is_used = 1U;
    pdev->ep_in[MTP_INT_EP & 0xFU].bInterval =
            (pdev->dev_speed == USBD_SPEED_HIGH) ? 0x08U : 0x10U;

    sInterface     = 0U;
    sCancelPending = 0U;
    sOpen          = true;

    if ((sCb != NULL) && (sCb->configured != NULL)) {
        sCb->configured(true);
    }
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_MTP_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);

    sOpen = false;

    (void)USBD_LL_CloseEP(pdev, MTP_IN_EP);
    pdev->ep_in[MTP_IN_EP & 0xFU].is_used = 0U;
    (void)USBD_LL_CloseEP(pdev, MTP_OUT_EP);
    pdev->ep_out[MTP_OUT_EP & 0xFU].is_used = 0U;
    (void)USBD_LL_CloseEP(pdev, MTP_INT_EP);
    pdev->ep_in[MTP_INT_EP & 0xFU].is_used = 0U;
    pdev->ep_in[MTP_INT_EP & 0xFU].bInterval = 0U;

    if ((sCb != NULL) && (sCb->configured != NULL)) {
        sCb->configured(false);
    }
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_MTP_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    static uint16_t sStatus;

    USBD_MTP_SetupCount = USBD_MTP_SetupCount + 1U;
    USBD_MTP_LastSetup  = ((uint32_t)req->bmRequest << 8) | req->bRequest;

    switch (req->bmRequest & USB_REQ_TYPE_MASK) {
    case USB_REQ_TYPE_CLASS:
        switch (req->bRequest) {
        case MTP_REQ_CANCEL:
            /* 6 bytes follow: cancellation code 0x4001 + transaction ID */
            if ((req->wLength >= 6U) && (req->wLength <= sizeof(sEp0Buf))) {
                sCancelPending = 1U;
                (void)USBD_CtlPrepareRx(pdev, sEp0Buf, req->wLength);
                return (uint8_t)USBD_OK;
            }
            break;

        case MTP_REQ_DEVICE_RESET:
            if ((sCb != NULL) && (sCb->cancel != NULL)) {
                sCb->cancel();
            }
            return (uint8_t)USBD_OK;

        case MTP_REQ_GET_DEVICE_STATUS:
            /* wLength, then the response code; no stalled endpoints listed */
            sEp0Buf[0] = 4U;
            sEp0Buf[1] = 0U;
            sEp0Buf[2] = LOBYTE(sBusy ? MTP_RESP_DEVICE_BUSY : MTP_RESP_OK);
            sEp0Buf[3] = HIBYTE(sBusy ? MTP_RESP_DEVICE_BUSY : MTP_RESP_OK);
            (void)USBD_CtlSendData(pdev, sEp0Buf, MIN(4U, req->wLength));
            return (uint8_t)USBD_OK;

        default:
            break;
        }
        USBD_CtlError(pdev, req);
        return (uint8_t)USBD_FAIL;

    case USB_REQ_TYPE_STANDARD:
        switch (req->bRequest) {
        case USB_REQ_GET_STATUS:
            sStatus = 0U;
            (void)USBD_CtlSendData(pdev, (uint8_t *)&sStatus, 2U);
            return (uint8_t)USBD_OK;
        case USB_REQ_GET_INTERFACE:
            (void)USBD_CtlSendData(pdev, &sInterface, 1U);
            return (uint8_t)USBD_OK;
        case USB_REQ_SET_INTERFACE:
            sInterface = (uint8_t)req->wValue;
            return (uint8_t)USBD_OK;
        case USB_REQ_CLEAR_FEATURE:
            return (uint8_t)USBD_OK;
        default:
            USBD_CtlError(pdev, req);
            return (uint8_t)USBD_FAIL;
        }

    default:
        USBD_CtlError(pdev, req);
        return (uint8_t)USBD_FAIL;
    }
}

static uint8_t USBD_MTP_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
    UNUSED(pdev);
    /* The composite calls every class here; only act on our own request. */
    if (sCancelPending != 0U) {
        sCancelPending = 0U;
        if ((sCb != NULL) && (sCb->cancel != NULL)) {
            sCb->cancel();
        }
    }
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_MTP_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    UNUSED(pdev);
    if (sCb == NULL) {
        return (uint8_t)USBD_OK;
    }
    if (epnum == (MTP_IN_EP & 0x7FU)) {
        if (sCb->txDone != NULL) {
            sCb->txDone();
        }
    } else if (epnum == (MTP_INT_EP & 0x7FU)) {
        if (sCb->eventDone != NULL) {
            sCb->eventDone();
        }
    }
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_MTP_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    uint32_t len = USBD_LL_GetRxDataSize(pdev, epnum);
    if ((sCb != NULL) && (sCb->rxDone != NULL)) {
        sCb->rxDone(len);
    }
    return (uint8_t)USBD_OK;
}

/* -------------------------------------------------------------------------- */

bool USBD_MTP_Receive(uint8_t *buf, uint32_t len)
{
    if (!sOpen || (sDev == NULL)) {
        return false;
    }
    return USBD_LL_PrepareReceive(sDev, MTP_OUT_EP, buf, len) == USBD_OK;
}

bool USBD_MTP_Transmit(const uint8_t *buf, uint32_t len)
{
    if (!sOpen || (sDev == NULL)) {
        return false;
    }
    sDev->ep_in[MTP_IN_EP & 0xFU].total_length = len;
    return USBD_LL_Transmit(sDev, MTP_IN_EP, (uint8_t *)buf, len) == USBD_OK;
}

bool USBD_MTP_SendEvent(const uint8_t *buf, uint32_t len)
{
    if (!sOpen || (sDev == NULL)) {
        return false;
    }
    return USBD_LL_Transmit(sDev, MTP_INT_EP, (uint8_t *)buf, len) == USBD_OK;
}

void USBD_MTP_AbortBulk(void)
{
    if (!sOpen || (sDev == NULL) || (sDev->pData == NULL)) {
        return;
    }
    /* Stop what is queued on both bulk endpoints, so the next command starts
       on idle ones. Flushing alone would leave an IN transfer armed. */
    PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)sDev->pData;
    (void)HAL_PCD_EP_Abort(hpcd, MTP_IN_EP);
    (void)HAL_PCD_EP_Flush(hpcd, MTP_IN_EP);
    (void)HAL_PCD_EP_Abort(hpcd, MTP_OUT_EP);
}

bool USBD_MTP_IsBusSuspended(void)
{
    return (sDev != NULL) && (sDev->dev_state == USBD_STATE_SUSPENDED);
}

uint16_t USBD_MTP_MaxPacket(void)
{
    if ((sDev != NULL) && (sDev->dev_speed == USBD_SPEED_HIGH)) {
        return MTP_MAX_HS_PACKET;
    }
    return MTP_MAX_FS_PACKET;
}

/* -------------------------------------------------------------------------- */

static uint8_t *USBD_MTP_GetHSCfgDesc(uint16_t *length)
{
    *length = (uint16_t)sizeof(USBD_MTP_CfgHSDesc);
    return USBD_MTP_CfgHSDesc;
}

static uint8_t *USBD_MTP_GetFSCfgDesc(uint16_t *length)
{
    *length = (uint16_t)sizeof(USBD_MTP_CfgFSDesc);
    return USBD_MTP_CfgFSDesc;
}

static uint8_t *USBD_MTP_GetOtherSpeedCfgDesc(uint16_t *length)
{
    *length = (uint16_t)sizeof(USBD_MTP_CfgFSDesc);
    return USBD_MTP_CfgFSDesc;
}

static uint8_t *USBD_MTP_GetDeviceQualifierDesc(uint16_t *length)
{
    *length = (uint16_t)sizeof(USBD_MTP_DeviceQualifierDesc);
    return USBD_MTP_DeviceQualifierDesc;
}

void USBD_Update_MTP_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep,
                          uint8_t out_ep, uint8_t int_ep, uint8_t str_idx)
{
    desc[11] = itf_no;
    desc[17] = str_idx;
    desc[20] = in_ep;
    desc[27] = out_ep;
    desc[34] = int_ep;

    MTP_IN_EP        = in_ep;
    MTP_OUT_EP       = out_ep;
    MTP_INT_EP       = int_ep;
    MTP_ITF_NBR      = itf_no;
    MTP_STR_DESC_IDX = str_idx;
}
