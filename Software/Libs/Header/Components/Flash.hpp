/**
 ******************************************************************************
 * @file    Flash.hpp
 * @brief   Provides access to device memory.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_FLASH_HPP
#define __COMPONENT_FLASH_HPP

#include <cstdint>
#include <cstdbool>

#include "IFlash.hpp"
#include "IGpio.hpp"
#include "OS.hpp"

class Flash : virtual public Interface::IFlash,
        virtual public Interface::IFlash::CpltHandler {

public:

    /**
     * @brief   Get Flash instance.
     * @retval  Flash instance.
     */
    static Flash& GetInstance()
    {
        static Flash sInstance;
        return sInstance;
    }

    /**
     * @brief   Sets the flash power mode to "always on".
     * The power switch is always on and the FLASH memory regulates
     * its own power consumption.
     */
    void powerModeAlwaysOn();

    /**
     * @brief   Sets the flash memory power mode to "auto".
     * The power switch turns on on request and turns off after FLASH
     * has not been accessed for some time.
     */
    void powerModeAuto();

    /**
     * @brief   Initialization of the Flash instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() override;

    /**
     * @brief   Deinitialization of the Flash instance.
     */
    virtual bool deinit() override;

    /**
     * @brief   Get block size in bytes.
     * @retval  Block size in bytes.
     */
    virtual uint32_t blockGetSize() override;

    /**
     * @brief   Get number of blocks.
     * @retval  Number of blocks.
     */
    virtual uint32_t blockGetCount() override;

    /**
     * @brief   Erases the specified block.
     * @param   blockIndex: block number
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool blockErase(uint32_t blockIndex, bool blocking = true) override;

    /**
     * @brief   Get sector size in bytes.
     * @retval  Sector size in bytes.
     */
    virtual uint32_t sectorGetSize() override;

    /**
     * @brief   Get number of sectors.
     * @retval  Number of sectors.
     */
    virtual uint32_t sectorGetCount() override;

    /**
     * @brief   Erases the specified sector.
     * @param   sectorIndex: sector number
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool sectorErase(uint32_t sectorIndex, bool blocking = true)
            override;

    /**
     * @brief   Write an amount of data to the memory.
     * @param   adr: Flash address.
     * @param   pData: Pointer to data buffer.
     * @param   size: Size of the data in bytes.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool memWrite(uint32_t adr, const uint8_t *pData, uint32_t size,
            bool blocking = true) override;

    /**
     * @brief   Read an amount of data from the memory.
     * @param   adr: Flash address.
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool memRead(uint32_t adr, uint8_t *pData, uint32_t size,
            bool blocking = true) override;

protected:

    /**
     * @brief   Constructor.
     */
    Flash() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Flash() = default;

private:

    // EMMC driver
    Interface::IFlash *pMmc = nullptr;

    // Power switch
    Interface::IGpo *pPwr = nullptr;

    // Indicates that the MMC port is initialized
    bool mIsInit = false;

    // Power mode
    bool mPowerModeAlwaysOn = false;

    // Idle timer
    OS::Timer<Flash> mPowerOffTimer;
    uint32_t mLastAccessTimestamp = 0;
    inline static constexpr uint32_t kPowerOffTimeout = 2000;

    // Actual EMMC state
    bool mIsMmcInit = false;

    // eMMC recovery: on repeated I/O failure the card/controller can wedge
    // (stops answering, holds D0 busy). A full power-cycle + re-init clears it.
    uint32_t mConsecErrors = 0;
    uint32_t mLastRecoverMs = 0;
    inline static constexpr uint32_t kRecoverThreshold = 2;
    inline static constexpr uint32_t kRecoverCooldownMs = 3000;

    // Serializes power-state transitions (power-on / power-off / recovery) so
    // the idle power-off (timer daemon) can never race an access and leave the
    // rail off with mIsMmcInit still set. The timer path uses a non-blocking
    // try-lock; accessor threads wait up to kPowerLockTimeoutMs.
    OS::Mutex mPowerMutex;
    inline static constexpr uint32_t kPowerLockTimeoutMs = 2000;

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler IMmcCpltHandler.
     */
    virtual void setCpltHandler(Interface::IFlash::CpltHandler *pHandler)
    override
    {
        // Not used
    }

    /**
     * @brief   Transfer complete interrupt handler
     * @param   pFlash: pointer to the IFlash instance.
     * @param   status: 'true' - success, 'false' - otherwise.
     */
    virtual void transferCplt(Interface::IFlash *pFlash, bool status) override;

    /**
     * @brief   Enable end initialize MMC.
     * @param   status: 'true' - success, 'false' - otherwise.
     */
    bool enableEMMC();

    /**
     * @brief   Power on the rail and initialize the card, retrying with a
     *          power-cycle on failure. Raises the caller priority around init
     *          to avoid an SDMMC RX OVERRUN.
     * @retval  'true' if the card initialized, 'false' otherwise.
     */
    bool powerOnInit();

    /**
     * @brief   Recover a wedged eMMC with a full power-cycle and re-init.
     *          Transparent to FatFs (sector data is unchanged). Caller must be
     *          in thread context with USB disconnected.
     * @retval  'true' if the card recovered, 'false' otherwise.
     */
    bool recover();

    /**
     * @brief   Decide whether recovery may run now and, if so, run it. Gated to
     *          thread context, battery mode (USB off), a failure threshold, and
     *          a cooldown so a permanently dead card cannot cause a recover storm.
     * @retval  'true' if recovery ran and succeeded (caller should retry the op).
     */
    bool maybeRecover();

    /**
     * @brief   Disable MMC.
     */
    void disableEMMC();

    /**
     * @brief   Power Off Timer Handler
     */
    void powerOffHandler(void *arg);

};

#endif /* __COMPONENT_FLASH_HPP */
