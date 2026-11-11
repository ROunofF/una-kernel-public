/*
 ******************************************************************************
 * @file    BlueNRG2.cpp
 * @brief   BlueNRG-2N network processor low-level driver.
 ******************************************************************************
 */

#include "BlueNRG2.hpp"

#define LOG_MODULE_PRX   "BLE.BlueNRG2"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "Logger.h"

#include "una_ble.h"

#include <cassert>
#include "OS.hpp"

#include "Sleep.hpp"

namespace Hardware
{

BlueNRG2 *BlueNRG2::mpInstance;

// ----------------------------------------------------------------------------

BlueNRG2::BlueNRG2(Interface::ISpi *pSpi, Interface::IGpo *pSpiCs,
        Interface::IExti *pIrq, Interface::IGpo *pRst)
{
    config(pSpi, pSpiCs, pIrq, pRst);
}

BlueNRG2::~BlueNRG2()
{
    DeInit();
    mpInstance = nullptr;
}

bool BlueNRG2::config(Interface::ISpi *pSpi, Interface::IGpo *pSpiCs,
        Interface::IExti *pIrq, Interface::IGpo *pRst)
{
    assert(pSpi   != nullptr);
    assert(pSpiCs != nullptr);
    assert(pIrq   != nullptr);
    assert(pRst   != nullptr);

    mpSpi   = pSpi;
    mpSpiCs = pSpiCs;
    mpIrq   = pIrq;
    mpRst   = pRst;

    mpInstance    = this;
    mIsConfigured = true;

    return true;
}

// ----------------------------------------------------------------------------

void BlueNRG2::IrqHandler()
{
    assert(mpInstance->mIsConfigured);

    uint32_t iterations = 0;

    while (GetIrq()) {
        if (una_ble_hci_notify_asynch_evt(nullptr) == 1) {
            // Read pool exhausted mid-pass. Events still pending in the chip keep
            // IRQ asserted with no fresh edge, so this pass stops; they are
            // drained once the next outbound command re-arms the IRQ edge (RX
            // self-heals without intervention). A proactive re-drain here would
            // only shave that latency, so it is omitted.
            return;
        }

        if (++iterations >= skMaxDrainIterations) {
            // IRQ is still asserted after far more reads than the pool can hold,
            // so this is not a real event burst but a wedged coprocessor holding
            // IRQ high (or feeding zero-length reads). Abort the drain so the HCI
            // mutex is released and the BLE monitor can reset the chip, rather
            // than spinning here forever and stranding recovery. Recovery is left
            // to the monitor by design: re-arming or resetting from here would
            // either re-enter this spin or pull reset orchestration down into the
            // bus driver.
            LOG_WARNING("IRQ stuck high, aborting drain (chip wedged?)\n");
            return;
        }
    }
}

// ----------------------------------------------------------------------------

int32_t BlueNRG2::Init(void *pConf)
{
    if (mpInstance == nullptr) {
        return -1;
    }

    assert(mpInstance->mIsConfigured);

    mpInstance->mpIrq->init(Interface::IGpi::Pull::NO_PULL,
            Interface::IExti::Edge::RISING, 5);

    mpInstance->mpRst->init(Interface::IGpo::Type::OD, false);

    // De-assert CS at startup to avoid spurious commands
    mpInstance->mpSpiCs->init(Interface::IGpo::Type::PP, true);

    if (mpInstance->mpSpi->init()) {
        mpInstance->mpSpi->setMode(Interface::ISpi::Mode::kMode1);
    } else {
        return -1;
    }

    return 0;
}

int32_t BlueNRG2::DeInit()
{
    mpInstance->mpIrq->enableInt(false);
    mpInstance->mpRst->set(false);

    mpInstance->mpSpi->deinit();
    mpInstance->mpSpiCs->deinit();
    mpInstance->mpIrq->deinit();
    mpInstance->mpRst->deinit();

    return 0;
}

// ----------------------------------------------------------------------------

/**
 * @brief Read transaction (called when IRQ is already HIGH before CS asserted).
 *
 * Protocol steps (datasheet Figure 16 - SPI read transaction):
 *  1. IRQ is HIGH - device has pending data, t2 >= 0.5 us setup already met
 *     because IRQ rose before this function was called from IrqHandler.
 *  2. Assert CS LOW.
 *  3. Exchange 5-byte header [0x0B, 0x00, 0x00, 0x00, 0x00].
 *     MISO returns [junk, WBUF_L, WBUF_H, RBUF_L, RBUF_H].
 *     byte[0] is a timing artifact (CPHA=1: slave not yet driven when
 *     master clocks the first byte), valid data starts at byte[1].
 *  4. Read RBUF bytes of payload.
 *  5. BlueNRG-2N lowers IRQ after header (may happen during data phase).
 *  6. Wait for IRQ LOW - MUST occur before CS is released.
 *  7. Release CS HIGH.
 */
int32_t BlueNRG2::SpiReceive(uint8_t *buffer, uint16_t size)
{
    if (mpInstance == nullptr) {
        return -1;
    }

    assert(mpInstance->mIsConfigured);

    uint8_t header_master[skHeaderSize] = { 0x0b, 0x00, 0x00, 0x00, 0x00 };
    uint8_t header_slave[skHeaderSize]  = { };

    DisableIrq();
    Driver::SleepGuard guard;   // resumes the sleep pause on every return path below

    // Step 2: Assert CS
    mpInstance->mpSpiCs->set(false);

    // Step 3: Exchange header
    bool transfer_ok = mpInstance->mpSpi->transfer(header_master, header_slave, skHeaderSize);

    if (!transfer_ok) {
        // SPI hardware failure - header_slave content is undefined,
        // do not inspect RBUF. Release CS and abort.
        LOG_ERROR("SPI header transfer failed\n");
        mpInstance->mpSpiCs->set(true);
        EnableIrq();
        return -4;
    }

    // RBUF is bytes [3..4], LSB first (datasheet Figure 15)
    uint16_t byte_count = (((uint16_t)header_slave[4]) << 8)
                        | ((uint16_t)header_slave[3]);

    if (byte_count == 0xFFFF) {
        // MISO line floating - abort cleanly
        LOG_WARNING("MISO floating (rbuf=0xFFFF)\n");
        byte_count = 0;
    }

    // Step 4: Read payload
    if (byte_count > 0) {
        if (byte_count > size) {
            byte_count = size;
        }
        mpInstance->mpSpi->receive(buffer, byte_count);
    }

    // Step 6: Wait for IRQ LOW before releasing CS.
    // Violation: raising CS while IRQ is HIGH causes BlueNRG-2N to
    // lower IRQ only after CS goes HIGH (error recovery path in device).
    uint32_t tickstart = GetTick();
    while (GetIrq()) {
        if ((GetTick() - tickstart) > skIrqLowTimeout) {
            LOG_WARNING("timeout waiting for IRQ LOW\n");
            break;
        }
        Delay(1);
    }

    // Step 7: Release CS
    mpInstance->mpSpiCs->set(true);

    EnableIrq();

    return (int32_t)byte_count;
}

// ----------------------------------------------------------------------------

/**
 * @brief Write transaction.
 *
 * Protocol steps (datasheet Figure 17 - SPI write transaction):
 *  1. Assert CS LOW - wakes BlueNRG-2N if sleeping.
 *  2. Wait for IRQ HIGH (t1 >= 0, up to ~1.82 ms worst case after wakeup).
 *  3. Exchange 5-byte header [0x0A, 0x00, 0x00, 0x00, 0x00].
 *     MISO returns [junk, WBUF_L, WBUF_H, RBUF_L, RBUF_H].
 *     byte[0] is a timing artifact (CPHA=1: slave not yet driven when
 *     master clocks the first byte), valid data starts at byte[1].
 *  4. BlueNRG-2N lowers IRQ after header (may happen during data phase).
 *  5. If WBUF >= size, transmit payload; otherwise release CS and retry.
 *  6. Wait for IRQ LOW - MUST occur before CS is released.
 *  7. Release CS HIGH.
 *  8. Inter-transaction delay - avoids 30 us wakeup-controller hazard.
 */
int32_t BlueNRG2::SpiSend(uint8_t *buffer, uint16_t size)
{
    if (mpInstance == nullptr) {
        return -1;
    }

    assert(mpInstance->mIsConfigured);

    int32_t  result        = 0;
    uint32_t retry         = 0;

    uint8_t header_master[skHeaderSize] = { 0x0a, 0x00, 0x00, 0x00, 0x00 };
    uint8_t header_slave[skHeaderSize]  = { };

    const uint32_t op_start = GetTick();

    // Timing probes (all in ms, measured from op_start)
    uint32_t t_irq_high  = 0;  // how long we waited for IRQ HIGH
    uint32_t t_transfer  = 0;  // header + data transfer duration
    uint32_t t_irq_low   = 0;  // how long we waited for IRQ LOW
    uint32_t t_total     = 0;  // total call duration

    DisableIrq();
    Driver::SleepGuard guard;   // resumes the sleep pause on every return/break path below

    do {
        result = 0;

        // Step 1: Assert CS - starts transaction, wakes device if sleeping
        mpInstance->mpSpiCs->set(false);

        // Step 2: Wait for IRQ HIGH
        // t1 worst case: wakeup from sleep + context restore ~1.82 ms.
        // skIrqHighTimeout = 10 ms provides x5 safety margin.
        uint32_t t = GetTick();
        while (!GetIrq()) {
            if ((GetTick() - t) > skIrqHighTimeout) {
                t_irq_high = GetTick() - t;
                LOG_WARNING(
                    "timeout waiting for IRQ HIGH"
                    " t_irq_high=%ums t_elapsed=%ums\n",
                    t_irq_high, (GetTick() - op_start));
                result = -3;
                break;
            }
            Delay(1);
        }
        t_irq_high = GetTick() - t;

        if (result == -3) {
            mpInstance->mpSpiCs->set(true);

            // Check if this is a simple wakeup delay: wait one more timeout
            // window without CS. If IRQ rises, the device just needed more
            // time - retry immediately.
            Delay(skIrqHighTimeout);
            uint32_t t_probe = GetTick();
            bool woke_up = false;
            while ((GetTick() - t_probe) < skIrqHighTimeout) {
                if (GetIrq()) { woke_up = true; break; }
                Delay(1);
            }

            if (woke_up) {
                result = -2;
                LOG_WARNING("woke_up\n");
                continue;
            }

            // Device is busy with a BLE radio event and has a pending IRQ to
            // deliver. Radio-busy retry inside SpiSend cannot succeed because
            // IRQ is disabled for the duration of this call - IrqHandler
            // cannot run to drain the pending event via SpiReceive. The device
            // will not accept a new SPI command until that event is consumed.
            // Return -3 so the caller can: enable IRQ (done automatically on
            // function exit), let IrqHandler drain the event, then retry.
            LOG_WARNING("device busy (BLE radio) - retry after IrqHandler drains pending event\n");
            break;
        }

        // Step 3: Exchange header
        t = GetTick();
        bool transfer_ok = mpInstance->mpSpi->transfer(header_master, header_slave, skHeaderSize);
        t_transfer = GetTick() - t;

        if (!transfer_ok) {
            // SPI hardware failure - header_slave content is undefined,
            // do not inspect WBUF. Release CS and break immediately;
            // retry cannot help until the bus is recovered.
            LOG_ERROR("SPI header transfer failed\n");
            result = -4;
            mpInstance->mpSpiCs->set(true);
            break;
        }

        // WBUF is bytes [1..2], LSB first (datasheet Figure 15)
        uint16_t wbuf = (((uint16_t)header_slave[2]) << 8)
                      | ((uint16_t)header_slave[1]);

        if (wbuf == 0xFFFF) {
            // MISO line floating - treat as not ready, retry
            LOG_WARNING("MISO floating (wbuf=0xFFFF)\n");
            result = -2;
        } else if (wbuf >= size) {
            // Steps 4-5: Send payload
            mpInstance->mpSpi->transmit(buffer, size);
            result = 0;  // success
        } else {
            // WBUF too small - device RX buffer full, retry after delay
            LOG_WARNING("WBUF too small: need=%u avail=%u\n", size, wbuf);
            result = -2;
        }

        // Step 6: Wait for IRQ LOW before releasing CS.
        // IRQ normally falls during or right after the data phase.
        t = GetTick();
        while (GetIrq()) {
            if ((GetTick() - t) > skIrqLowTimeout) {
                t_irq_low = GetTick() - t;
                LOG_WARNING(
                    "timeout waiting for IRQ LOW"
                    " t_irq_low=%ums t_elapsed=%ums\n",
                    t_irq_low, (GetTick() - op_start));
                break;
            }
            Delay(1);
        }
        t_irq_low = GetTick() - t;

        // Step 7: Release CS
        mpInstance->mpSpiCs->set(true);

        // Step 8: Inter-transaction delay.
        // After CS HIGH the device transitions: Transaction_Complete ->
        // Configured -> Sleep. A hardware hazard in the wakeup controller
        // causes IRQ to never assert if the next CS LOW arrives within
        // ~30 us of the device entering sleep (one 32 kHz period).
        // 2 ms safely clears this window on every retry.
        Delay(skInterTxDelayMs);

        retry++;

        if ((GetTick() - op_start) > skTimeoutDuration) {
            LOG_WARNING("operation timeout after %u retries\n", retry);
            result = -3;
            break;
        }

    } while (result < 0);

    t_total = GetTick() - op_start;

    LOG_DEBUG(
        "sz=%u result=%d t_irq_hi=%u t_xfer=%u t_irq_lo=%u t_tot=%u ms\n",
        size, result, t_irq_high, t_transfer, t_irq_low, t_total);

    // Suppress unused-variable warnings when logging is disabled.
    (void)t_irq_high; (void)t_transfer; (void)t_irq_low; (void)t_total;

    EnableIrq();

    return result;
}

// ----------------------------------------------------------------------------

/**
 * @brief Hardware reset of BlueNRG-2N.
 *
 * Sequence (datasheet Figure 4 - power-up sequence):
 *  1. De-assert CS to prevent spurious SPI activity during reset.
 *  2. Drive RESETN LOW for skResetPulseMs (min 30 us, datasheet).
 *  3. Release RESETN HIGH.
 *  4. Wait skResetBootMs for firmware to boot (CPU active within 1.82 ms,
 *     BLE stack initialization takes additional time).
 *  5. Wait for IRQ HIGH - device signals readiness after stack init.
 */
int32_t BlueNRG2::Reset()
{
    if (mpInstance == nullptr) {
        return -1;
    }

    assert(mpInstance->mIsConfigured);

    DisableIrq();
    Driver::SleepGuard guard;   // restores the sleep pause on every return path below

    LOG_DEBUG("asserting RESETN\n");

    // Step 1: De-assert CS
    mpInstance->mpSpiCs->set(true);

    // Step 2: Assert RESETN LOW
    mpInstance->mpRst->set(false);
    Delay(skResetPulseMs);

    // Step 3: Release RESETN HIGH
    mpInstance->mpRst->set(true);

    // Step 4: Wait for firmware boot
    Delay(skResetBootMs);

    // Step 5: Wait for IRQ HIGH - device ready
    // Use absolute deadline to avoid signed/unsigned overflow in comparison
    const uint32_t deadline = (uint32_t)GetTick() + skResetTimeout;
    while (!GetIrq()) {
        if ((uint32_t)GetTick() >= deadline) {
            LOG_ERROR("timeout waiting for IRQ HIGH\n");
            EnableIrq();
            return -1;
        }
        Delay(1);
    }

    LOG_DEBUG("done\n");

    EnableIrq();

    return 0;
}

// ----------------------------------------------------------------------------

int32_t BlueNRG2::GetTick()
{
    return OS::GetTimeMs();
}

int32_t BlueNRG2::Delay(uint32_t ms)
{
    OS::Delay(ms);
    return 0;
}

bool BlueNRG2::GetIrq()
{
    if (mpInstance == nullptr) {
        return false;
    }

    assert(mpInstance->mIsConfigured);

    return mpInstance->mpIrq->get();
}

void BlueNRG2::DisableIrq()
{
    mpInstance->mpIrq->enableInt(false);
}

void BlueNRG2::EnableIrq()
{
    mpInstance->mpIrq->enableInt(true);
}

} /* namespace Hardware */
