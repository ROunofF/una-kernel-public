/*
 ******************************************************************************
 * @file    BlueNRG2.hpp
 * @brief   BlueNRG-2N network processor low-level driver.
 ******************************************************************************
 */

#ifndef __HARDWARE_BLUENRG2_HPP
#define __HARDWARE_BLUENRG2_HPP

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

#include "ISpi.hpp"
#include "IGpio.hpp"

namespace Hardware
{

/**
 * @brief BlueNRG-2N network processor low-level driver.
 *        Implements the SPI slave protocol required by the BlueNRG library.
 */
class BlueNRG2
{

public:

    /**
     * @brief Default constructor.
     */
    BlueNRG2() = default;

    /**
     * @brief Constructor with hardware resources.
     * @param pSpi    Pointer to SPI interface.
     * @param pSpiCs  Pointer to chip-select GPIO.
     * @param pIrq    Pointer to IRQ EXTI line.
     * @param pRst    Pointer to reset GPIO (open-drain, active LOW).
     */
    BlueNRG2(Interface::ISpi *pSpi, Interface::IGpo *pSpiCs,
            Interface::IExti *pIrq, Interface::IGpo *pRst);

    /**
     * @brief Destructor.
     */
    virtual ~BlueNRG2();

    /**
     * @brief Bind hardware resources after default construction.
     * @param pSpi    Pointer to SPI interface.
     * @param pSpiCs  Pointer to chip-select GPIO.
     * @param pIrq    Pointer to IRQ EXTI line.
     * @param pRst    Pointer to reset GPIO (open-drain, active LOW).
     * @return true on success.
     */
    bool config(Interface::ISpi *pSpi, Interface::IGpo *pSpiCs,
            Interface::IExti *pIrq, Interface::IGpo *pRst);

    /**
     * @brief IRQ pin interrupt handler. Must be called from EXTI ISR.
     */
    void IrqHandler();

    // ------------------------------------------------------------------
    // Static API — called by the BlueNRG library through function pointers
    // ------------------------------------------------------------------

    /**
     * @brief  Initialize SPI and GPIO peripherals.
     * @param  pConf  Unused configuration pointer (reserved).
     * @return 0 on success, negative on error.
     */
    static int32_t Init(void *pConf = nullptr);

    /**
     * @brief  Deinitialize all peripherals.
     * @return 0 on success, negative on error.
     */
    static int32_t DeInit();

    /**
     * @brief  Read data from BlueNRG-2N via SPI (read transaction).
     *         Called from IrqHandler when IRQ is HIGH.
     * @param  buffer  Destination buffer.
     * @param  size    Buffer capacity in bytes.
     * @return Number of bytes read, or negative on error.
     */
    static int32_t SpiReceive(uint8_t *buffer, uint16_t size);

    /**
     * @brief  Write data to BlueNRG-2N via SPI (write transaction).
     *         Retries automatically when WBUF is too small (up to skTimeoutDuration).
     *         On first IRQ HIGH timeout probes for wakeup completion and retries once.
     * @param  buffer  Source data buffer.
     * @param  size    Number of bytes to send.
     * @retval  0   Success.
     * @retval -2   WBUF consistently too small within skTimeoutDuration.
     * @retval -3   Device busy with a BLE radio event (pending IRQ). IRQ is
     *              re-enabled on return so IrqHandler can drain the event;
     *              caller should retry immediately. Reset if persistent.
     * @retval -4   SPI hardware transfer failure. Bus recovery or chip reset
     *              required before retrying.
     */
    static int32_t SpiSend(uint8_t *buffer, uint16_t size);

    /**
     * @brief  Hardware reset of BlueNRG-2N.
     *         Pulses RESETN LOW for skResetPulseMs, then waits for IRQ HIGH.
     * @return 0 on success, negative on timeout.
     */
    static int32_t Reset();

    /**
     * @brief  Return system time in milliseconds.
     * @return Current tick count in ms.
     */
    static int32_t GetTick();

    /**
     * @brief  Blocking delay.
     * @param  ms  Delay in milliseconds.
     * @return 0 on success
     */
    static int32_t Delay(uint32_t ms);

    /**
     * @brief  Read current level of the IRQ pin.
     * @return true if IRQ is HIGH (event pending or device ready).
     */
    static bool GetIrq();

    /**
     * @brief  Disable IRQ EXTI interrupt (used around SPI transactions).
     */
    static void DisableIrq();

    /**
     * @brief  Enable IRQ EXTI interrupt.
     */
    static void EnableIrq();

private:

    // ------------------------------------------------------------------
    // Timing constants — all values in milliseconds
    // ------------------------------------------------------------------

    // Minimum RESETN LOW pulse width.
    // Datasheet: minimum 30 us. Use 5 ms for robust margin.
    static constexpr uint32_t skResetPulseMs = 5;

    // Wait after RESETN HIGH before polling IRQ.
    // Datasheet power-up sequence: CPU active within 1.82 ms after POR.
    // The BLE stack then initialises and raises IRQ to signal readiness.
    // 150 ms gives ample margin for firmware boot.
    static constexpr uint32_t skResetBootMs = 150;

    // Timeout for IRQ HIGH after RESETN released.
    // If IRQ does not go HIGH within this time, the chip has not booted.
    static constexpr uint32_t skResetTimeout = 2500;

    // Timeout waiting for IRQ HIGH after CS assertion (t1).
    // Worst case: wakeup from sleep + software context restore ~1.82 ms.
    // 10 ms provides a x5 safety margin.
    static constexpr uint32_t skIrqHighTimeout = 10;

    // Timeout waiting for IRQ LOW before releasing CS (end of transaction).
    // IRQ normally falls during or immediately after data transfer.
    // 2 ms is sufficient for internal BlueNRG-2N processing at 16 MHz.
    static constexpr uint32_t skIrqLowTimeout = 2;

    // Total timeout for the SpiSend retry loop (WBUF-full retries).
    static constexpr uint32_t skTimeoutDuration = 100;

    // Mandatory inter-transaction delay after CS HIGH.
    // BlueNRG-2N transitions: Transaction_Complete -> Configured -> Sleep.
    // A known hardware hazard causes IRQ to never assert if the next CS LOW
    // arrives within ~30 us of the device entering sleep (one 32 kHz period).
    // 2 ms safely clears this window.
    static constexpr uint32_t skInterTxDelayMs = 2;

    // Cap on receive iterations per IrqHandler pass. Productive reads are bounded
    // by the read-pool size (each consumes a pool packet), so a pass longer than
    // the pool must be spinning on unproductive reads while IRQ stays high - a
    // wedged coprocessor. Set well above the pool so real event bursts never trip
    // it; on the cap the drain aborts to release the HCI mutex for recovery.
    static constexpr uint32_t skMaxDrainIterations = 32;

    // ------------------------------------------------------------------
    // SPI protocol constants
    // ------------------------------------------------------------------
    static constexpr uint32_t skHeaderSize    = 5;
    static constexpr uint32_t skMaxBufferSize = 255;

    // ------------------------------------------------------------------
    // Instance members
    // ------------------------------------------------------------------
    bool mIsConfigured { };

    Interface::ISpi  *mpSpi   { };
    Interface::IGpo  *mpSpiCs { };
    Interface::IExti *mpIrq   { };
    Interface::IGpo  *mpRst   { };

    // Singleton pointer used by static BlueNRG library callbacks
    static BlueNRG2 *mpInstance;
};

} /* namespace Hardware */

#endif /* __HARDWARE_BLUENRG2_HPP */
