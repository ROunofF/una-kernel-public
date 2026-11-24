/**
 ******************************************************************************
 * @file    usbd_mtp.h
 * @brief   MTP (Media Transfer Protocol) interface for the AL94 composite.
 *
 *          Only the USB side lives here: the descriptor, the endpoints and the
 *          PTP class requests. Everything runs in the USB interrupt and does
 *          no file work; packets are handed to MtpResponder, a task, through
 *          the callbacks below. That is what lets MTP use FatFs, which takes a
 *          mutex and so cannot be called from the interrupt the way the Mass
 *          Storage class reads the flash.
 ******************************************************************************
 */

#ifndef __USBD_MTP_H
#define __USBD_MTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "usbd_ioreq.h"

/* Config header + interface + bulk IN + bulk OUT + interrupt IN */
#define USB_MTP_CONFIG_DESC_SIZ     (9U + 9U + 7U + 7U + 7U)

#define MTP_MAX_FS_PACKET           64U
#define MTP_MAX_HS_PACKET           512U
#define MTP_EVENT_PACKET            28U     /* Event container: 12 + 3 params */

extern uint8_t MTP_IN_EP;
extern uint8_t MTP_OUT_EP;
extern uint8_t MTP_INT_EP;
extern uint8_t MTP_ITF_NBR;
extern uint8_t MTP_STR_DESC_IDX;
extern volatile uint32_t USBD_MTP_SetupCount;
extern volatile uint32_t USBD_MTP_LastSetup;

#define MTP_STR_DESC                "MTP"

extern USBD_ClassTypeDef USBD_MTP;

/**
 * Callbacks into the responder. All of them run in the USB interrupt.
 */
typedef struct
{
    void (*configured)(bool on);        /* Interface opened (true) or closed */
    void (*rxDone)(uint32_t len);       /* Bulk OUT transfer finished */
    void (*txDone)(void);               /* Bulk IN transfer finished */
    void (*eventDone)(void);            /* Interrupt IN transfer finished */
    void (*cancel)(void);               /* Cancel or Device Reset request */
} USBD_MTP_Callbacks;

void USBD_MTP_SetCallbacks(const USBD_MTP_Callbacks *cb);

/** Set from the responder: whether it is between operations, for the
 *  Get Device Status request. */
void USBD_MTP_SetBusy(bool busy);

/* Called from the responder task. False when the interface is not open. */
bool     USBD_MTP_Receive(uint8_t *buf, uint32_t len);
bool     USBD_MTP_Transmit(const uint8_t *buf, uint32_t len);
bool     USBD_MTP_SendEvent(const uint8_t *buf, uint32_t len);
void     USBD_MTP_AbortBulk(void);
uint16_t USBD_MTP_MaxPacket(void);
/* The host has suspended the bus (Windows does after 30 s idle): an event
   would wait until it resumes. */
bool     USBD_MTP_IsBusSuspended(void);

void USBD_Update_MTP_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep,
                          uint8_t out_ep, uint8_t int_ep, uint8_t str_idx);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_MTP_H */
