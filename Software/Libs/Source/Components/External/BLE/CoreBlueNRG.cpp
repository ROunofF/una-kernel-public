/**
 ******************************************************************************
 * @file    CoreBlueNRG.cpp
 * @brief   BlueNRG core component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "CoreBlueNRG.hpp"

#include <cstring>

#define LOG_MODULE_PRX      "CoreBlueNRG"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>

namespace Ble
{

CoreBlueNRG *CoreBlueNRG::pInstance;

CoreBlueNRG::CoreBlueNRG()
{
    mFlags.init(LOG_MODULE_PRX);
    mMutex.init(LOG_MODULE_PRX, true);
    mHciMutex.init("CoreBlueNRG.HCI", true);
    mWorkQueue.init("CoreBlueNRG.Work");

    mIrqServiceThread.init("CoreBlueNRG.IRQ", this,
            &CoreBlueNRG::irqServiceThread,
            // Since this task replaces an interrupt, its priority
            // must be higher than the BLE task
            osPriorityAboveNormal);

    mHciServiceThread.init("CoreBlueNRG.HCI", this,
            &CoreBlueNRG::hciServiceThread,
            osPriorityNormal);

    pInstance = this;
}

CoreBlueNRG::~CoreBlueNRG()
{
    deinit();
    pInstance = nullptr;
}

void CoreBlueNRG::config(Interface::ISpi *pSpi, Interface::IGpo *pSpiCs,
        Interface::IExti *pIrq, Interface::IGpo *pRst)
{
    assert(pSpi != nullptr);
    assert(pSpiCs != nullptr);
    assert(pIrq != nullptr);
    assert(pRst != nullptr);

    mMutex.lock();

    mpIrq = pIrq;
    mBlueNRG2.config(pSpi, pSpiCs, pIrq, pRst);

    mMutex.release();
}

bool CoreBlueNRG::init()
{
    mMutex.lock();

    if (mFlags.isSet(Flag::Initialized)) {
        mMutex.release();
        return true;
    }

    // Link Low level with library
    una_ble_hci_init();

    // Register IO bus services after una_ble_hci_init(), in oder
    // to not perform reset immediately
    // Do reset before BLE on.
    una_ble_hci_io fops { };
    fops.init = mBlueNRG2.Init;
    fops.deinit = mBlueNRG2.DeInit;
    fops.send = spiSendLocked;
    fops.receive = mBlueNRG2.SpiReceive;
    fops.reset = mBlueNRG2.Reset;
    fops.get_tick = mBlueNRG2.GetTick;
    fops.delay = mBlueNRG2.Delay;
    una_ble_hci_register_io_bus(&fops);

    // Must be before run irqServiceThread
    mBlueNRG2.Init();

    // Enable interrupts handling after reset (una_ble_hci_init())
    mpIrq->setExtiHandler(this);

    LOG_DEBUG("Initialized\n");
    mFlags.set(Flag::Initialized);

    mMutex.release();

    return true;
}

int32_t CoreBlueNRG::spiSendLocked(uint8_t *buffer, uint16_t size)
{
    // Bus lock timeout. Well above the worst legitimate hold (a burst drain of
    // several receives under one IrqHandler pass, tens of ms) yet below the BLE
    // monitor cadence and the watchdog window. Skipping the transmit on timeout
    // is safe (the bus is simply not touched) and surfaces as a command failure
    // the caller already handles, instead of a silent forever-wait that would
    // also park the BLE recovery path when a holder wedges.
    static constexpr uint32_t kBusLockTimeoutMs = 1000;

    // A BlueNRG-2N SPI frame is atomic from CS-low to CS-high; a command
    // transmit that interleaves with an in-flight event receive corrupts data on
    // both sides and can desync command framing. The receive runs under
    // mHciMutex on the IRQ service thread, so take the same mutex here to keep
    // transmit and receive off the bus at the same time.
    if (pInstance == nullptr) {
        return -1;
    }

    if (!pInstance->mHciMutex.lock(kBusLockTimeoutMs)) {
        LOG_WARNING("SPI send: bus lock timeout\n");
        return -1;
    }

    int32_t result = pInstance->mBlueNRG2.SpiSend(buffer, size);
    pInstance->mHciMutex.release();

    return result;
}

bool CoreBlueNRG::deinit()
{
    mMutex.lock();

    if (!mFlags.isSet(Flag::Initialized)) {
        mMutex.release();
        return true;
    }

    // Waiting for the end of communication with the chip
    mHciMutex.lock();

    mFlags.clear(Flag::Initialized);
    mEnabled = false;

    mBlueNRG2.DeInit();

    mHciMutex.release();

    LOG_DEBUG("Deinitialized\n");
    mMutex.release();
    return true;
}

bool CoreBlueNRG::enable()
{
    // Base initialization
    una_ble_status status { };
    uint8_t hwVersion = 0;
    uint16_t fwVersion = 0;
    uint8_t bdaddr[6] { };
    uint8_t bdaddrLenOut = 0;
    // Offset of the static random address stored in NVM
    const uint8_t configDataStoredStaticRandomAddress = 0x80;


    mMutex.lock();

    if (!mFlags.isSet(Flag::Initialized)) {
        goto exit;
    }

    if (mEnabled) {
        goto exit;
    }


    // Reset chip
    mFlags.clear(Flag::Boot);
    mBlueNRG2.Reset();

    // During a physical reset, interrupt (IRQ pin) is disabled and
    // will be enabled only after the reset procedure is completed.
    // We expect that immediately after enabling the interrupt
    // it will fire, which will result in the processing of the
    // first event (set Flag::Boot)
    if (!waitForBoot()) {
        LOG_ERROR("Boot timeout\n");
        goto exit;
    }

    // Get the BlueNRG HW and FW versions
    if (!getVersion(hwVersion, fwVersion)) {
        LOG_ERROR("Read version error\n");
        goto exit;
    }
    LOG_INFO("Version: HW %X, FW %X\n", hwVersion, fwVersion);

    // Set the TX power. Can be from 0 to 7 (7 = 8.0 dBm - default)
    status = una_ble_hal_set_tx_power_level(1, 7);
    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_ERROR("una_ble_hal_set_tx_power_level() failed [0x%02X]\n", status);
        goto exit;
    }

    status = una_ble_hal_read_config_data(configDataStoredStaticRandomAddress,
            &bdaddrLenOut, bdaddr);
    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_ERROR("una_ble_hal_read_config_data() failed [0x%02X]\n", status);
        goto exit;
    }

    if ((bdaddr[5] & 0xC0) != 0xC0) {
        LOG_ERROR("Random Static Address not well formed\n");
        goto exit;
    }
    LOG_INFO("Random Static Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
            bdaddr[5],bdaddr[4],bdaddr[3],bdaddr[2],bdaddr[1],bdaddr[0]);


    mEnabled = true;
    LOG_DEBUG("Enabled\n");
exit:
    mMutex.release();

    return mEnabled;
}

bool CoreBlueNRG::disable()
{
    mMutex.lock();

    if (!mEnabled) {
        mMutex.release();
        return true;
    }

    // Waiting for the end of communication with the BlueNRG chip
    mHciMutex.lock();

    mEnabled = false;
    mBlueNRG2.Reset();

    LOG_DEBUG("Disabled\n");

    mHciMutex.release();
    mMutex.release();
    return false;
}

bool CoreBlueNRG::isEnabled()
{
    return mEnabled;
}

void CoreBlueNRG::setResetHandler(ResetHandler *pHandler)
{
    mpResetHandler = pHandler;
}

void CoreBlueNRG::lock()
{
    mMutex.lock();
}

void CoreBlueNRG::release()
{
    mMutex.release();
}

bool CoreBlueNRG::startTxFCC(uint8_t channel)
{
    // Must be initial state
    if (!mFlags.isSet(Flag::Initialized) && mEnabled) {
        return false;
    }

    bool result = false;
    mMutex.lock();

    mFlags.clear(Flag::Boot);
    mBlueNRG2.Reset();

    // During a physical reset, interrupt (IRQ pin) is disabled and
    // will be enabled only after the reset procedure is completed.
    // We expect that immediately after enabling the interrupt
    // it will fire, which will result in the processing of the
    // first event (set Flag::Boot)
    if (waitForBoot()) {
        una_ble_status status;
        status = una_ble_hal_set_tx_power_level(1, 4);
        if (status == UNA_BLE_STATUS_SUCCESS) {
            OS::Delay(100);
            status = una_ble_hci_le_transmitter_test(channel, 0x25, 0x00);
            if (status == UNA_BLE_STATUS_SUCCESS) {
                LOG_INFO("TX test started on channel %u\n", channel);
                result = true;
            } else {
                LOG_ERROR("una_ble_hci_le_transmitter_test failed 0x%02X\n", status);
            }
        } else {
            LOG_ERROR("una_ble_hal_set_tx_power_level failed 0x%02X\n", status);
        }
    }

    mMutex.release();
    return result;
}

bool CoreBlueNRG::startRxFCC(uint8_t channel)
{
    // Must be initial state
    if (!mFlags.isSet(Flag::Initialized) && mEnabled) {
        return false;
    }

    bool result = false;
    mMutex.lock();

    mFlags.clear(Flag::Boot);
    mBlueNRG2.Reset();

    if (waitForBoot()) {
        una_ble_status status = una_ble_hci_le_receiver_test(channel);
        if (status == UNA_BLE_STATUS_SUCCESS) {
            LOG_INFO("RX test started on channel %u\n", channel);
            result = true;
        } else {
            LOG_ERROR("una_ble_hci_le_receiver_test failed 0x%02X\n", status);
        }
    }

    mMutex.release();
    return result;
}

bool CoreBlueNRG::stopTxFCC(uint32_t &numPackets)
{
    bool result = false;
    mMutex.lock();

    una_ble_status status;
    uint16_t numPacketsRx = 0; // Not used for TX test

    status = una_ble_hci_le_test_end(&numPacketsRx);
    if (status == UNA_BLE_STATUS_SUCCESS) {
        status = una_ble_hal_le_tx_test_packet_number(&numPackets);
        if (status == UNA_BLE_STATUS_SUCCESS) {
            LOG_INFO("TX test ended. Packets sent: %lu\n", numPackets);
            result = true;
        } else {
            LOG_ERROR("una_ble_hal_le_tx_test_packet_number failed 0x%02X\n", status);
        }
    } else {
        LOG_ERROR("una_ble_hci_le_test_end failed 0x%02X\n", status);
    }

    mMutex.release();
    return result;
}

bool CoreBlueNRG::stopRxFCC(uint32_t &numPackets)
{
    bool result = false;
    mMutex.lock();

    una_ble_status status;
    uint16_t numPacketsRx = 0;

    status = una_ble_hci_le_test_end(&numPacketsRx);
    if (status == UNA_BLE_STATUS_SUCCESS) {
        numPackets = numPacketsRx;
        LOG_INFO("RX test ended. Packets received: %lu\n", numPackets);
        result = true;
    } else {
        LOG_ERROR("una_ble_hci_le_test_end failed 0x%02X\n", status);
    }

    mMutex.release();
    return result;
}

bool CoreBlueNRG::runRxFCCScan(const uint8_t *channels, uint8_t count,
        uint32_t dwellMs, Interface::IBle::RxFCCScanResult *results)
{
    if (channels == nullptr || results == nullptr || count == 0) {
        return false;
    }

    // Must be initial state
    if (!mFlags.isSet(Flag::Initialized) && mEnabled) {
        return false;
    }

    bool allOk = true;
    mMutex.lock();

    mFlags.clear(Flag::Boot);
    mBlueNRG2.Reset();

    if (!waitForBoot()) {
        LOG_ERROR("runRxFCCScan: boot timeout\n");
        mMutex.release();
        return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        results[i].channel    = channels[i];
        results[i].numPackets = 0;
        results[i].ok         = false;

        // Start receiver on this channel
        una_ble_status status = una_ble_hci_le_receiver_test(channels[i]);
        if (status != UNA_BLE_STATUS_SUCCESS) {
            LOG_ERROR("una_ble_hci_le_receiver_test ch %u failed 0x%02X\n",
                    channels[i], status);
            allOk = false;
            // Try to continue with remaining channels
            continue;
        }

        // Release mutex during dwell so IRQ/HCI threads can run
        mMutex.release();
        OS::Delay(dwellMs);
        mMutex.lock();

        // Stop and collect result
        uint16_t numPacketsRx = 0;
        status = una_ble_hci_le_test_end(&numPacketsRx);
        if (status == UNA_BLE_STATUS_SUCCESS) {
            results[i].numPackets = numPacketsRx;
            results[i].ok         = true;
            LOG_INFO("RX scan ch %u: %u packets\n", channels[i], numPacketsRx);
        } else {
            LOG_ERROR("una_ble_hci_le_test_end ch %u failed 0x%02X\n",
                    channels[i], status);
            allOk = false;
        }
    }

    mMutex.release();
    return allOk;
}

void CoreBlueNRG::extiHandler(Interface::IExti *pExti, Interface::IExti::Edge edge)
{
    mIrqServiceThread.flagsSet(Flag::IrqNotify);
}

void CoreBlueNRG::irqServiceThread()
{
    for (;;) {
        // Block until complete initialization
        mFlags.waitAllNoClear(Flag::Initialized);
        for (;;) {
            mIrqServiceThread.flagsWait(Flag::IrqNotify);

            if (!mFlags.isSet(Flag::Initialized)) {
                break;
            }

            mHciMutex.lock();
            mBlueNRG2.IrqHandler();
            mHciMutex.release();

            mHciServiceThread.flagsSet(Flag::HciNotify);
        }
    }
}

void CoreBlueNRG::hciServiceThread()
{
    for (;;) {
        // Block until complete initialization
        mFlags.waitAllNoClear(Flag::Initialized);

        for (;;) {
            mHciServiceThread.flagsWait(Flag::HciNotify | Flag::BleWorkNotify);

            if (!mFlags.isSet(Flag::Initialized)) {
                break;
            }

            // This thread is intended for processing events from the BlueNRG coprocessor,
            // BUT NOT for processing expected responses to commands.
            // We use a mutex when calling the BlueNRG API so that this thread does not
            // intercept responses to commands.
            mMutex.lock();
            if (mFlags.isSet(Flag::Initialized)) {
                una_ble_hci_user_evt_proc();
            }
            mMutex.release();

            // Run work marshalled onto this thread (e.g. discovery/GATT-client
            // setup ticks). Kept out of the mutex above: each posted step takes
            // the core lock itself for the commands it issues, and running it
            // here already serialises it with event processing (same thread).
            drainWork();
        }
    }
}

bool CoreBlueNRG::post(void (*fn)(void*), void* ctx)
{
    if (fn == nullptr) {
        return false;
    }

    if (!mWorkQueue.push({ fn, ctx }, 0)) {
        LOG_WARNING("BLE work queue full, work dropped\n");
        return false;
    }

    mHciServiceThread.flagsSet(Flag::BleWorkNotify);
    return true;
}

void CoreBlueNRG::drainWork()
{
    // One item per HCI-loop pass. A drained step can block in an ACI command (up
    // to HCI_DEFAULT_TIMEOUT_MS), during which this thread cannot run
    // una_ble_hci_user_evt_proc() and so cannot dispatch an incoming notification (e.g.
    // an accessory HR sample). Running a single step and re-signalling lets the
    // loop pump events between steps, so a connect-time setup burst cannot
    // head-of-line-block notifications across several back-to-back stalls.
    WorkItem item {};
    if (mWorkQueue.pop(item, 0)) {
        item.fn(item.ctx);
        if (!mWorkQueue.empty()) {
            mHciServiceThread.flagsSet(Flag::BleWorkNotify);
        }
    }
}

bool CoreBlueNRG::ping()
{
    uint8_t hwVersion = 0;
    uint16_t fwVersion = 0;
    return getVersion(hwVersion, fwVersion);
}

bool CoreBlueNRG::getVersion(uint8_t &hwVersion, uint16_t &fwVersion)
{
    una_ble_status status { };

#if 0 // Print detailed FW info
    uint8_t  DTM_version_major, DTM_version_minor, DTM_version_patch, DTM_variant;
    uint16_t DTM_Build_Number;
    uint8_t  BTLE_Stack_version_major, BTLE_Stack_version_minor, BTLE_Stack_version_patch,
             BTLE_Stack_development;
    uint16_t BTLE_Stack_variant, BTLE_Stack_Build_Number;
    uint8_t  BTLE_sv_patch, DTM_v_patch;
    const uint8_t alphabet[] =
            { ' ', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z' };

    status = una_ble_hal_get_firmware_details(&DTM_version_major, &DTM_version_minor,
                                          &DTM_version_patch, &DTM_variant, &DTM_Build_Number,
                                          &BTLE_Stack_version_major, &BTLE_Stack_version_minor,
                                          &BTLE_Stack_version_patch, &BTLE_Stack_development,
                                          &BTLE_Stack_variant, &BTLE_Stack_Build_Number);

    BTLE_sv_patch = alphabet[BTLE_Stack_version_patch];
    DTM_v_patch = alphabet[DTM_version_patch];

    LOG_DEBUG("  BlueNRG-2 FW v%d.%d%c\n",
            BTLE_Stack_version_major, BTLE_Stack_version_minor, BTLE_sv_patch);

    LOG_DEBUG("  BTLE_Stack: dev %d, var 0x%02X, build %d\n",
            BTLE_Stack_development, BTLE_Stack_variant, BTLE_Stack_Build_Number);

    LOG_DEBUG("  DTM %s v%d.%d%c, buid %d\n",
            DTM_variant == 0x01 ? "UART" : (DTM_variant == 0x02 ? "SPI" : "unknown"),
            DTM_version_major, DTM_version_minor, DTM_v_patch, DTM_Build_Number);

#endif

    uint8_t hciVersion = 0;
    uint8_t lmpPalVersion = 0;
    uint16_t hciRevision = 0;
    uint16_t manufacturerName = 0;
    uint16_t lmpPalSubversion = 0;

    status = una_ble_hci_read_local_version_information(&hciVersion, &hciRevision,
            &lmpPalVersion, &manufacturerName, &lmpPalSubversion);

    if (status == UNA_BLE_STATUS_SUCCESS) {
        hwVersion = hciRevision >> 8;
        fwVersion = (hciRevision & 0xFF) << 8;              // Major Version Number
        fwVersion |= ((lmpPalSubversion >> 4) & 0xF) << 4;  // Minor Version Number
        fwVersion |= lmpPalSubversion & 0xF;                // Patch Version Number
    }

    return status == UNA_BLE_STATUS_SUCCESS;
}

bool CoreBlueNRG::waitForBoot()
{
    int32_t cnt = 100;
    while (cnt--) {
        if (mFlags.wait(Flag::Boot, osFlagsWaitAny, 1)) {
            return true;
        }
        una_ble_hci_user_evt_proc();
    }
    return false;
}


}  /* namespace Ble */



extern "C" {

void una_ble_blue_initialized_event(uint8_t Reason_Code)
{
    Ble::CoreBlueNRG *instance = Ble::CoreBlueNRG::pInstance;
    assert(instance != nullptr);

    if (Reason_Code == 0x01) {
        // Firmware started properly
        instance->mFlags.set(Ble::CoreBlueNRG::Flag::Boot);
    } else if (Reason_Code >= 0x02 && Reason_Code <= 0x04) {
        // Updater mode
        // Do nothing
    } else if (Reason_Code >= 0x05 && Reason_Code <= 0x09) {
        // Spontaneous system reset (watchdog/lockup/brownout/crash/ECC). The
        // coprocessor has rebooted blank: no GATT/GAP and not advertising, while
        // our cached host state still believes BLE is enabled/advertising. Clear
        // the cached enabled flag and notify the peripheral so it invalidates its
        // own state; the BLE watchdog (Backend::bleMonitorTimCallback) then
        // re-enables and re-advertises on its next tick.
        LOG_ERROR("BlueNRG system reset, recovering\n");
        instance->mEnabled = false;
        if (instance->mpResetHandler != nullptr) {
            instance->mpResetHandler->onReset();
        }
    } else {
        // Do nothing
    }
}

} /* extern "C" */
