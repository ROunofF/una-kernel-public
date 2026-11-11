/**
 ******************************************************************************
 * @file    N24S64B.cpp
 * @brief   64 Kb Serial EEPROM with I2C interface
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "N24S64B.hpp"

#define LOG_MODULE_PRX      "N24S64B"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>
#include "System.hpp"

namespace Hardware
{

N24S64B::N24S64B(Interface::II2c &i2c, uint8_t devAddr)
{
    config(i2c, devAddr);
}

bool N24S64B::config(Interface::II2c &i2c, uint8_t devAddr)
{
    assert(devAddr <= 0x07); // Only 3 bits allowed

    mpI2c = &i2c;
    mDevAddr = devAddr & 0x07; // Ensure only 3 bits

    mIsConfigured = true;

    return true;
}

bool N24S64B::init()
{
    assert(mIsConfigured == true);

    // Check if device is ready
    if (!isReady()) {
        LOG_ERROR("Device not ready\n");
        return false;
    }

    // Read and verify device configuration
    DeviceConfig config;
    if (!readDeviceConfig(config)) {
        LOG_ERROR("Failed to read device configuration\n");
        return false;
    }

    // Check if device address matches
    uint8_t configuredAddr = (config.a2 << 2) | (config.a1 << 1) | config.a0;
    if (configuredAddr != mDevAddr) {
        LOG_WARNING("Device address mismatch. Expected: 0x%02X, Got: 0x%02X\n",
                   mDevAddr, configuredAddr);

        // Update device configuration to match expected address
        config.a2 = (mDevAddr >> 2) & 0x01;
        config.a1 = (mDevAddr >> 1) & 0x01;
        config.a0 = mDevAddr & 0x01;

        if (!writeDeviceConfig(config)) {
            LOG_ERROR("Failed to update device configuration\n");
            return false;
        }

        // Wait for write completion
        delay(skWriteCycleTimeMs + 1);

        LOG_INFO("Device address updated to 0x%02X\n", mDevAddr);
    }

    LOG_INFO("N24S64B initialized successfully. Address: 0x%02X, SWP: %s\n",
             mDevAddr, config.swp ? "enabled" : "disabled");

    return true;
}

bool N24S64B::isReady()
{
    assert(mIsConfigured == true);

    return mpI2c->isDeviceReady(getMemoryI2cAddr());
}

bool N24S64B::readUniqueId(UniqueId_t &uniqueId)
{
    assert(mIsConfigured == true);

    // Step 1: Send dummy write to set address for Unique ID read
    // Address bytes: xxxx x01x xxxx 0000 (according to Table 8)
    uint8_t addrBytes[2] = {0x02, 0x00}; // xxxx x01x xxxx 0000

    bool ret = mpI2c->writeMem(getSpecialI2cAddr(), addrBytes[0], 1, &addrBytes[1], 1);

    uint32_t retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry dummy write for Unique ID\n");
        ret = mpI2c->writeMem(getSpecialI2cAddr(), addrBytes[0], 1, &addrBytes[1], 1);
    }

    if (!ret) {
        LOG_ERROR("Failed to send dummy write for Unique ID\n");
        return false;
    }

    // Step 2: Read Unique ID using immediate read
    ret = mpI2c->receive(getSpecialI2cAddr(), uniqueId.bytes, skUniqueIdSize);

    retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry Unique ID read\n");
        ret = mpI2c->receive(getSpecialI2cAddr(), uniqueId.bytes, skUniqueIdSize);
    }

    if (!ret) {
        LOG_ERROR("Failed to read Unique ID\n");
        return false;
    }

    LOG_DEBUG("Unique ID read successfully\n");
    return true;
}

bool N24S64B::getUniqueIdStr(char *strBuffer)
{
    assert(strBuffer != nullptr);

    UniqueId_t uniqueId;
    if (!readUniqueId(uniqueId)) {
        return false;
    }

    // Convert to hex string
    for (int i = 0; i < skUniqueIdSize; i++) {
        sprintf(&strBuffer[i * 2], "%02X", uniqueId.bytes[i]);
    }
    strBuffer[skUniqueIdSize * 2] = '\0'; // Null terminator

    return true;
}

bool N24S64B::readDeviceConfig(DeviceConfig &config)
{
    assert(mIsConfigured == true);

    // Step 1: Send dummy write to set address for Device Configuration read
    // Address bytes: xxxx x11x xxxx xxxx (according to Table 8)
    uint8_t addrBytes[2] = {0x06, 0x00}; // xxxx x11x xxxx xxxx

    bool ret = mpI2c->writeMem(getSpecialI2cAddr(), addrBytes[0], 1, &addrBytes[1], 1);

    if (!ret) {
        LOG_ERROR("Failed to send dummy write for Device Config\n");
        return false;
    }

    // Step 2: Read Device Configuration using immediate read
    ret = mpI2c->receive(getSpecialI2cAddr(), &config.reg, 1);

    if (!ret) {
        LOG_ERROR("Failed to read Device Configuration\n");
        return false;
    }

    return true;
}

bool N24S64B::writeDeviceConfig(const DeviceConfig &config)
{
    assert(mIsConfigured == true);

    // Device Configuration Register Write
    // Address bytes: xxxx x11x xxxx xxxx (according to Table 8)
    uint8_t addrBytes[2] = {0x06, 0x00}; // xxxx x11x xxxx xxxx
    uint8_t dataBytes[3] = {addrBytes[0], addrBytes[1], config.reg};

    bool ret = mpI2c->transmit(getSpecialI2cAddr(), dataBytes, 3);

    uint32_t retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry Device Config write\n");
        ret = mpI2c->transmit(getSpecialI2cAddr(), dataBytes, 3);
    }

    if (!ret) {
        LOG_ERROR("Failed to write Device Configuration\n");
        return false;
    }

    // Note: Device Configuration Register Write does not support acknowledge polling
    // Must wait tWR = 5 ms
    delay(skWriteCycleTimeMs);

    return true;
}

bool N24S64B::readByte(uint16_t address, uint8_t &data)
{
    assert(mIsConfigured == true);
    assert(address < skMemorySize);

    // Selective read: set address then read
    uint8_t addrBytes[2] = {
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF)
    };

    bool ret = mpI2c->writeMem(getMemoryI2cAddr(), addrBytes[0], 1, &addrBytes[1], 1);

    if (!ret) {
        LOG_ERROR("Failed to set address for read\n");
        return false;
    }

    // Immediate read
    ret = mpI2c->receive(getMemoryI2cAddr(), &data, 1);

    if (!ret) {
        LOG_ERROR("Failed to read byte at address 0x%04X\n", address);
        return false;
    }

    return true;
}

bool N24S64B::writeByte(uint16_t address, uint8_t data)
{
    assert(mIsConfigured == true);
    assert(address < skMemorySize);

    // Byte write: address + data
    uint8_t writeData[3] = {
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)(address & 0xFF),
        data
    };

    bool ret = mpI2c->transmit(getMemoryI2cAddr(), writeData, 3);

    uint32_t retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry byte write\n");
        ret = mpI2c->transmit(getMemoryI2cAddr(), writeData, 3);
    }

    if (!ret) {
        LOG_ERROR("Failed to write byte at address 0x%04X\n", address);
        return false;
    }

    // Wait for write completion using acknowledge polling
    return waitWriteComplete();
}

bool N24S64B::writeBytes(uint16_t address, const uint8_t *pData, uint16_t length)
{
    assert(mIsConfigured == true);
    assert(pData != nullptr);
    assert(address + length <= skMemorySize);

    if (length == 0) {
        return true;
    }

    bool ret = mpI2c->writeMem(getMemoryI2cAddr(), address, 2, pData, length);

    if (!ret) {
        LOG_ERROR("Failed to set address for sequential read\n");
        return false;
    }

    return true;
}

bool N24S64B::readBytes(uint16_t address, uint8_t *pData, uint16_t length)
{
    assert(mIsConfigured == true);
    assert(pData != nullptr);
    assert(address + length <= skMemorySize);

    if (length == 0) {
        return true;
    }

    bool ret = mpI2c->readMem(getMemoryI2cAddr(), address, 2, pData, length);

    if (!ret) {
        LOG_ERROR("Failed to set address for sequential read\n");
        return false;
    }

    return true;
}

bool N24S64B::writePage(uint16_t address, const uint8_t *pData, uint8_t length)
{
    assert(mIsConfigured == true);
    assert(pData != nullptr);
    assert(length > 0 && length <= skPageSize);
    assert(address < skMemorySize);

    // Check that write doesn't cross page boundary
    uint16_t pageStart = address & ~(skPageSize - 1);
    uint16_t pageEnd = pageStart + skPageSize - 1;
    if (address + length - 1 > pageEnd) {
        LOG_ERROR("Write crosses page boundary\n");
        return false;
    }

    // Page write: address + data
    uint8_t writeBuffer[skPageSize + 2]; // Max page size + 2 address bytes
    writeBuffer[0] = (uint8_t)((address >> 8) & 0xFF);
    writeBuffer[1] = (uint8_t)(address & 0xFF);

    for (uint8_t i = 0; i < length; i++) {
        writeBuffer[2 + i] = pData[i];
    }

    bool ret = mpI2c->transmit(getMemoryI2cAddr(), writeBuffer, length + 2);

    uint32_t retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry page write\n");
        ret = mpI2c->transmit(getMemoryI2cAddr(), writeBuffer, length + 2);
    }

    if (!ret) {
        LOG_ERROR("Failed to write page at address 0x%04X\n", address);
        return false;
    }

    // Wait for write completion using acknowledge polling
    return waitWriteComplete();
}

bool N24S64B::readSecurePage(uint8_t address, uint8_t *pData, uint8_t length)
{
    assert(mIsConfigured == true);
    assert(pData != nullptr);
    assert(address < skSecureDataPageSize);
    assert(length > 0);

    // Step 1: Send dummy write to set address for Secure Data Page read
    // Address bytes: xxxx x00x xxxx xxxx (according to Table 8)
    uint8_t addrBytes[2] = {0x00, address}; // xxxx x00x address_in_page

    bool ret = mpI2c->writeMem(getSpecialI2cAddr(), addrBytes[0], 1, &addrBytes[1], 1);

    if (!ret) {
        LOG_ERROR("Failed to send dummy write for Secure Page read\n");
        return false;
    }

    // Step 2: Read from Secure Data Page using immediate read
    ret = mpI2c->receive(getSpecialI2cAddr(), pData, length);

    if (!ret) {
        LOG_ERROR("Failed to read from Secure Page\n");
        return false;
    }

    return true;
}

bool N24S64B::writeSecurePage(uint8_t address, const uint8_t *pData, uint8_t length)
{
    assert(mIsConfigured == true);
    assert(pData != nullptr);
    assert(address < skSecureDataPageSize);
    assert(length > 0);

    // Check if secure page is locked
    if (isSecurePageLocked()) {
        LOG_ERROR("Secure page is locked\n");
        return false;
    }

    // Secure Data Page Write
    // Address bytes: xxxx x00x xxxx xxxx (according to Table 8)
    uint8_t writeBuffer[skSecureDataPageSize + 2];
    writeBuffer[0] = 0x00; // xxxx x00x
    writeBuffer[1] = address;

    for (uint8_t i = 0; i < length; i++) {
        writeBuffer[2 + i] = pData[i];
    }

    bool ret = mpI2c->transmit(getSpecialI2cAddr(), writeBuffer, length + 2);

    uint32_t retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry Secure Page write\n");
        ret = mpI2c->transmit(getSpecialI2cAddr(), writeBuffer, length + 2);
    }

    if (!ret) {
        LOG_ERROR("Failed to write to Secure Page\n");
        return false;
    }

    // Wait for write completion
    return waitWriteComplete();
}

bool N24S64B::lockSecurePage()
{
    assert(mIsConfigured == true);

    // Secure Data Page Lock
    // Address bytes: xxxx x10x xxxx xxxx (according to Table 8)
    uint8_t lockData[3] = {0x04, 0x00, 0xFF}; // xxxx x10x, don't care, all 1s

    bool ret = mpI2c->transmit(getSpecialI2cAddr(), lockData, 3);

    uint32_t retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry Secure Page lock\n");
        ret = mpI2c->transmit(getSpecialI2cAddr(), lockData, 3);
    }

    if (!ret) {
        LOG_ERROR("Failed to lock Secure Page\n");
        return false;
    }

    // Wait for write completion
    return waitWriteComplete();
}

bool N24S64B::isSecurePageLocked()
{
    assert(mIsConfigured == true);

    // Step 1: Send dummy write to set address for lock status read
    // Address bytes: xxxx x10x xxxx xxxx (according to Table 8)
    uint8_t addrBytes[2] = {0x04, 0x00}; // xxxx x10x xxxx xxxx

    bool ret = mpI2c->writeMem(getSpecialI2cAddr(), addrBytes[0], 1, &addrBytes[1], 1);

    if (!ret) {
        LOG_ERROR("Failed to send dummy write for lock status\n");
        return false; // Assume unlocked on error
    }

    // Step 2: Read lock status
    uint8_t lockStatus = 0;
    ret = mpI2c->receive(getSpecialI2cAddr(), &lockStatus, 1);

    if (!ret) {
        LOG_ERROR("Failed to read lock status\n");
        return false; // Assume unlocked on error
    }

    // Bit 1 indicates lock status: 1 = locked, 0 = unlocked
    return (lockStatus & 0x02) != 0;
}

bool N24S64B::setSoftwareWriteProtection(bool enable)
{
    DeviceConfig config;
    if (!readDeviceConfig(config)) {
        return false;
    }

    config.swp = enable ? 1 : 0;

    return writeDeviceConfig(config);
}

bool N24S64B::waitWriteComplete()
{
    uint32_t startTime = getTick();

    while ((getTick() - startTime) < skWriteTimeoutMs) {
        if (mpI2c->isDeviceReady(getMemoryI2cAddr())) {
            return true;
        }
        delay(1);
    }

    LOG_ERROR("Write timeout\n");
    return false;
}

uint8_t N24S64B::getMemoryI2cAddr() const
{
    // Memory array access: 1010 A2A1A0 (where A2A1A0 are device address bits)
    return skI2cBaseAddr | (mDevAddr & 0x07);
}

uint8_t N24S64B::getSpecialI2cAddr() const
{
    // Special operations: 1011 A2A1A0 (where A2A1A0 are device address bits)
    return skI2cSpecialAddr | (mDevAddr & 0x07);
}

uint32_t N24S64B::getTick()
{
    return Driver::System::GetTimeMs();
}

void N24S64B::delay(uint32_t ms)
{
    Driver::System::Delay(ms);
}

} /* namespace Hardware */
