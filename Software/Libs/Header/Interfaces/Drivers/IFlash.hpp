/**
 ******************************************************************************
 * @file    IFlash.hpp
 * @brief   Flash component interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_FLASH_HPP
#define __INTERFACE_I_FLASH_HPP

#include <cstdint>
#include <cstdbool>

namespace Interface
{

/**
 * @brief   Flash component interface.
 */
class IFlash
{

public:

    /**
     * @brief   Initialization.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief   Deinitialization.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() = 0;

    /**
     * @brief   Get block size in bytes.
     * @retval  Block size in bytes.
     */
    virtual uint32_t blockGetSize() = 0;

    /**
     * @brief   Get number of blocks.
     * @retval  Number of blocks.
     */
    virtual uint32_t blockGetCount() = 0;

    /**
     * @brief   Erases the specified block.
     * @param   blockIndex: block number
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool blockErase(uint32_t blockIndex, bool blocking = true) = 0;

    /**
     * @brief   Get sector size in bytes.
     * @retval  Sector size in bytes.
     */
    virtual uint32_t sectorGetSize() = 0;

    /**
     * @brief   Get number of sectors.
     * @retval  Number of sectors.
     */
    virtual uint32_t sectorGetCount() = 0;

    /**
     * @brief   Erases the specified sector.
     * @param   sectorIndex: sector number
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool sectorErase(uint32_t sectorIndex, bool blocking = true) = 0;

    /**
     * @brief   Write an amount of data to the memory.
     * @param   adr: Memory address.
     * @param   pData: Pointer to data buffer.
     * @param   size: Size of the data in bytes.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool memWrite(uint32_t adr, const uint8_t *pData, uint32_t size,
            bool blocking = true) = 0;

    /**
     * @brief   Read an amount of data from the memory.
     * @param   adr: Memory address.
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool memRead(uint32_t adr, uint8_t *pData, uint32_t size,
            bool blocking = true) = 0;

    /**
     * @brief   Flash transfer complete interrupt handler interface.
     */
    class CpltHandler
    {
    public:
        /**
         * @brief   Transfer complete interrupt handler
         * @param   pFlash: pointer to the IFlash instance.
         * @param   status: 'true' - success, 'false' - otherwise.
         */
        virtual void transferCplt(Interface::IFlash *pFlash, bool status) = 0;
    };

    /**
     * @brief   Flash transfer complete interrupt handler.
     * @param   pHandler: interrupt handler IFlash::CpltHandler.
     */
    virtual void setCpltHandler(Interface::IFlash::CpltHandler *pHandler) = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IFlash() = default;

};

} /* namespace Interface */

#endif /* __INTERFACE_I_FLASH_HPP */
