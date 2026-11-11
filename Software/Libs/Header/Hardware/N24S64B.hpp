/**
 ******************************************************************************
 * @file    N24S64B.hpp
 * @brief   64 Kb Serial EEPROM with I2C interface
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __HARDWARE_N24S64B_HPP
#define __HARDWARE_N24S64B_HPP

#include <stdint.h>
#include <stdbool.h>

#include "II2c.hpp"

namespace Hardware
{

/**
 * @brief   N24S64B 64Kb Serial EEPROM with I2C interface
 */
class N24S64B
{

public:

    // Base I2C address (bits A2,A1,A0 are configurable via Device Configuration Register)
    static const uint8_t skI2cBaseAddr = 0x50;          // 1010 000x (for memory array access)
    static const uint8_t skI2cSpecialAddr = 0x58;       // 1011 000x (for special operations)

    // Memory organization
    static const uint16_t skMemorySize = 8192;          // 64 Kb = 8192 bytes
    static const uint8_t skPageSize = 32;               // 32 bytes per page
    static const uint16_t skNumPages = 256;             // 256 pages total

    // Unique ID size
    static const uint8_t skUniqueIdSize = 16;           // 128 bits = 16 bytes

    // Secure Data Page size
    static const uint8_t skSecureDataPageSize = 32;     // 32 bytes

    // Write cycle time
    static const uint8_t skWriteCycleTimeMs = 5;        // 5ms max write time

    /**
     * @brief   Device Configuration Register structure
     */
    union DeviceConfig {
        struct {
            uint8_t reserved1   :1; // b0 - reserved
            uint8_t swp         :1; // b1 - Software Write Protection
            uint8_t reserved2   :3; // b2-b4 - reserved
            uint8_t a0          :1; // b5 - Device Address bit 0
            uint8_t a1          :1; // b6 - Device Address bit 1
            uint8_t a2          :1; // b7 - Device Address bit 2
        };
        uint8_t reg;
    };

    /**
     * @brief   Unique ID structure (128 bits / 16 bytes)
     */
    typedef struct {
        uint8_t bytes[skUniqueIdSize];
    } UniqueId_t;

    /**
     * @brief   Constructor.
     */
    N24S64B() = default;

    /**
     * @brief   Constructor.
     * @param   i2c: reference to I2C interface.
     * @param   devAddr: Target device address (3-bit configurable part). Default 0.
     */
    N24S64B(Interface::II2c &i2c, uint8_t devAddr = 0);

    /**
     * @brief   Destructor.
     */
    virtual ~N24S64B() = default;

    /**
     * @brief   Configure chip with I2C driver.
     * @param   i2c: reference to I2C interface.
     * @param   devAddr: Target device address (3-bit configurable part). Default 0.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool config(Interface::II2c &i2c, uint8_t devAddr = 0);

    /**
     * @brief   Initialize the EEPROM.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool init();

    /**
     * @brief   Checks if chip is ready for communication.
     * @retval  Execution status. 'true' - device ready, 'false' - device not responded.
     */
    bool isReady();

    /**
     * @brief   Read Unique ID Number (128-bit factory-set read-only).
     * @param   uniqueId: reference to store the unique ID.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readUniqueId(UniqueId_t &uniqueId);

    /**
     * @brief   Get Unique ID as string (32 hex characters).
     * @param   strBuffer: buffer to store string (minimum 33 bytes for null terminator).
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool getUniqueIdStr(char *strBuffer);

    /**
     * @brief   Read Device Configuration Register.
     * @param   config: reference to store device configuration.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readDeviceConfig(DeviceConfig &config);

    /**
     * @brief   Write Device Configuration Register.
     * @param   config: new device configuration.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool writeDeviceConfig(const DeviceConfig &config);

    /**
     * @brief   Read byte from memory array.
     * @param   address: memory address (0 to 8191).
     * @param   data: reference to store read data.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readByte(uint16_t address, uint8_t &data);

    /**
     * @brief   Write byte to memory array.
     * @param   address: memory address (0 to 8191).
     * @param   data: data to write.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool writeByte(uint16_t address, uint8_t data);

    /**
     * @brief   Write array to memory array.
     * @param   address: memory address (0 to 8191).
     * @param   pData:   pointer to buffer for writedata.
     * @param   length:  number of bytes to write.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool writeBytes(uint16_t address, const uint8_t *pData, uint16_t length);

    /**
     * @brief   Read multiple bytes from memory array (sequential read).
     * @param   address: starting memory address.
     * @param   pData: pointer to buffer for read data.
     * @param   length: number of bytes to read.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readBytes(uint16_t address, uint8_t *pData, uint16_t length);

    /**
     * @brief   Write page to memory array (up to 32 bytes).
     * @param   address: starting memory address.
     * @param   pData: pointer to data to write.
     * @param   length: number of bytes to write (max 32).
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool writePage(uint16_t address, const uint8_t *pData, uint8_t length);

    /**
     * @brief   Read from Secure Data Page.
     * @param   address: address within secure page (0-31).
     * @param   pData: pointer to buffer for read data.
     * @param   length: number of bytes to read.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readSecurePage(uint8_t address, uint8_t *pData, uint8_t length);

    /**
     * @brief   Write to Secure Data Page.
     * @param   address: address within secure page (0-31).
     * @param   pData: pointer to data to write.
     * @param   length: number of bytes to write.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool writeSecurePage(uint8_t address, const uint8_t *pData, uint8_t length);

    /**
     * @brief   Lock Secure Data Page against future writes.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool lockSecurePage();

    /**
     * @brief   Check if Secure Data Page is locked.
     * @retval  'true' if locked, 'false' if unlocked or error.
     */
    bool isSecurePageLocked();

    /**
     * @brief   Enable/disable software write protection.
     * @param   enable: true to enable protection, false to disable.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setSoftwareWriteProtection(bool enable);

    /**
     * @brief   Wait for write cycle completion using acknowledge polling.
     * @retval  Execution status. 'true' if success, 'false' if timeout.
     */
    bool waitWriteComplete();

private:

    // Indicates that the chip is configured
    bool mIsConfigured = false;

    // Hardware configuration
    uint8_t mDevAddr = 0;                    // 3-bit configurable device address
    Interface::II2c *mpI2c = nullptr;

    // Number of retries for I2C transactions
    static const uint32_t skI2cRetries = 4;

    // Timeout for write completion
    static const uint32_t skWriteTimeoutMs = 10;

    /**
     * @brief   Get actual I2C address for memory operations.
     * @retval  7-bit I2C address for memory array access.
     */
    uint8_t getMemoryI2cAddr() const;

    /**
     * @brief   Get actual I2C address for special operations.
     * @retval  7-bit I2C address for special operations.
     */
    uint8_t getSpecialI2cAddr() const;

    /**
     * @brief   Return system tick in ms.
     * @retval  Current HAL time base time stamp.
     */
    uint32_t getTick();

    /**
     * @brief   Delay.
     * @param   ms: time in milliseconds.
     */
    void delay(uint32_t ms);

};

} /* namespace Hardware */

#endif /* __HARDWARE_N24S64B_HPP */
