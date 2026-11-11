/**
 ******************************************************************************
 * @file    CoreBlueNRG.hpp
 * @brief   BlueNRG core component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_CORE_BLUENRG_HPP
#define __COMPONENT_BLE_CORE_BLUENRG_HPP

#include <stdint.h>
#include <stdbool.h>

#include "OS.hpp"

#include "IBle.hpp"
#include "ISpi.hpp"
#include "IGpio.hpp"
#include "IBleThreadWork.hpp"
#include "BlueNRG2.hpp"
#include "una_ble.h"

namespace Ble
{

/**
 * @brief   BlueNRG core component.
 */
class CoreBlueNRG : virtual public Interface::IExti::Handler,
        public IBleThreadWork
{

public:

    /**
     * @brief   Constructor.
     */
    CoreBlueNRG();

    /**
     * @brief   Destructor.
     */
    virtual ~CoreBlueNRG();

    /**
     * @brief   Configure with hardware resources.
     * @param   pSpi: pointer to the SPI.
     * @param   pSpiCs: pointer to the CS pin.
     * @param   pIrq: pointer to the IRQ pin.
     * @param   pRst: pointer to the RST pin.
     */
    void config(Interface::ISpi *pSpi, Interface::IGpo *pSpiCs,
            Interface::IExti *pIrq, Interface::IGpo *pRst);

    /**
     * @brief   Initialize BLE object.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool init();

    /**
     * @brief   Deinitialize BLE object and free all resources.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool deinit();

    /**
     * @brief   Turn on BLE.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool enable();

    /**
     * @brief   Turn off BLE or put it to low power mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool disable();

    /**
     * @brief   Check if the BLE is on.
     * @retval  'true' - BLE is on, 'false' - otherwise.
     */
    bool isEnabled();

    /**
     * @brief   Lock access to the BLE core.
     *          Use this method when call BlueNRG2 API.
     */
    void lock();

    /**
     * @brief   Unlock access to the BLE core.
     */
    void release();

    /**
     * @brief   Start FCC TX test (LE transmitter test).
     * @param   channel: RF channel index, N = (F - 2402) / 2, range 0x00..0x27.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool startTxFCC(uint8_t channel);

    /**
     * @brief   Start FCC RX test (LE receiver test).
     * @param   channel: RF channel index, N = (F - 2402) / 2, range 0x00..0x27.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool startRxFCC(uint8_t channel);

    /**
     * @brief   Stop FCC TX test and retrieve the number of transmitted packets.
     * @param   numPackets: reference to store the number of transmitted packets.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool stopTxFCC(uint32_t &numPackets);

    /**
     * @brief   Stop FCC RX test and retrieve the number of received packets.
     * @param   numPackets: reference to store the number of received packets.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool stopRxFCC(uint32_t &numPackets);

    /**
     * @brief   Run a multi-channel RX FCC scan without chip reset between channels.
     * @note    Resets the chip once before the first channel, then cycles through
     *          all channels using stop->start without additional resets.
     * @param   channels:  pointer to array of RF channel indices (0x00..0x27).
     * @param   count:     number of channels in the array.
     * @param   dwellMs:   receive window per channel in milliseconds.
     * @param   results:   caller-allocated array of at least 'count' entries.
     * @retval  'true' if all slots completed without HCI error, 'false' otherwise.
     */
    bool runRxFCCScan(const uint8_t *channels, uint8_t count,
            uint32_t dwellMs, Interface::IBle::RxFCCScanResult *results);

    /**
     * @brief   Check if the BlueNRG-2 chip is alive and responding.
     *          Performs a lightweight read of the local version information.
     * @retval  'true' - chip responded successfully, 'false' - otherwise.
     */
    bool ping();

    /**
     * @brief   Get BlueNRG-2 hardware and firmware version.
     * @param   hwVersion: hardware version.
     * @param   fwVersion: firmware version.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool getVersion(uint8_t &hwVersion, uint16_t &fwVersion);

    /**
     * @brief   Handler notified when the BlueNRG-2 coprocessor resets itself
     *          spontaneously (firmware watchdog, lockup, brownout, crash, ECC).
     * @note    onReset() runs in the BlueNRG event-processing thread with the
     *          core mutex held, so it must be lightweight and must not re-enter
     *          the BlueNRG API.
     */
    class ResetHandler {
    public:
        virtual void onReset() = 0;
    protected:
        virtual ~ResetHandler() = default;
    };

    /**
     * @brief   Register the handler notified on spontaneous coprocessor reset.
     * @param   pHandler: handler to notify, or nullptr to clear.
     */
    void setResetHandler(ResetHandler *pHandler);

    // --- IBleThreadWork ---------------------------------------------------

    /**
     * @brief   Queue a callback to run on the HCI (BLE event) thread.
     * @note    Serviced from the HCI thread's loop, after event processing.
     */
    bool post(void (*fn)(void*), void* ctx) override;

protected:

private:

    // The BlueNRG library is not designed to create multiple instances
    // and uses static callbacks. This static pointer is created to access
    // the CoreBlueNRG object.
    static CoreBlueNRG *pInstance;

    // Hardware resources.
    Interface::IExti *mpIrq { };
    Hardware::BlueNRG2 mBlueNRG2 { };

    OS::Thread<CoreBlueNRG, 1024> mIrqServiceThread { };
    OS::Thread<CoreBlueNRG, 5*1024> mHciServiceThread { };

    // Mutex for access to the BlueNRG API
    OS::Mutex mMutex { };

    // Serialises the SPI bus between the command-transmit path (spiSendLocked)
    // and the event-receive path (irqServiceThread), and gates BLE shutdown.
    // A BlueNRG-2N SPI frame is atomic from CS-low to CS-high, so a transmit and
    // a receive must never overlap on the bus.
    OS::Mutex mHciMutex { };

    OS::EventFlags mFlags { };

    struct Flag {
        static constexpr uint32_t Initialized   = (1 << 0);
        static constexpr uint32_t IrqNotify     = (1 << 1);
        static constexpr uint32_t HciNotify     = (1 << 2);
        static constexpr uint32_t BleWorkNotify = (1 << 3);
        static constexpr uint32_t Boot          = (1 << 4);
    };

    // Deferred work run on the HCI thread (see IBleThreadWork): timer callbacks
    // post their step here so it runs serialised with event processing rather
    // than on the RTOS timer-daemon task.
    struct WorkItem {
        void (*fn)(void*);
        void *ctx;
    };
    static constexpr uint8_t kWorkQueueLen = 8;
    OS::Queue<WorkItem, kWorkQueueLen> mWorkQueue { };

    /**
     * @brief   Run all queued deferred work (called on the HCI thread).
     */
    void drainWork();

    bool mRtosInitialized { };
    bool mEnabled {};

    // Notified when the coprocessor resets spontaneously (see ResetHandler).
    ResetHandler *mpResetHandler { nullptr };


    /**
     * @brief   External interrupt handler for the IRQ pin.
     * @param   pExti: pointer to the external interrupt instance IExti.
     * @param   edge: Interrupt edge Edge.
     */
    virtual void extiHandler(Interface::IExti *pExti,
            Interface::IExti::Edge edge) override;

    /**
     * @brief   Interrupts service thread
     */
    void irqServiceThread();

    /**
     * @brief   HCI service thread
     */
    void hciServiceThread();

    /**
     * @brief   Wait boot event from BlueNRG-2 stack.
     * @retval  'true' - BlueNRG-2 successfully booted, 'false' - otherwise.
     */
    bool waitForBoot();

    /**
     * @brief   IO-bus transmit callback registered with the HCI transport.
     * @note    Wraps BlueNRG2::SpiSend under mHciMutex so a command transmit is
     *          serialised against the event receive running on the IRQ service
     *          thread; the two must not share the SPI bus at the same time.
     * @param   buffer: pointer to the bytes to transmit.
     * @param   size: number of bytes to transmit.
     * @retval  Bytes transmitted, or a negative error code.
     */
    static int32_t spiSendLocked(uint8_t *buffer, uint16_t size);

    friend void ::una_ble_blue_initialized_event(uint8_t Reason_Code);

};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_CORE_BLUENRG_HPP */
