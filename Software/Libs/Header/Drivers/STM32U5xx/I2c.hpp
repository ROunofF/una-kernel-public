/**
 ******************************************************************************
 * @file    I2c.hpp
 * @brief   I2C driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef DRIVER_I2C_HPP
#define DRIVER_I2C_HPP

#include <cstdint>
#include <cstdbool>

#include "II2c.hpp"

#include "main.h"

namespace Driver
{

/**
 * @brief   I2C driver.
 */
class I2c : virtual public Interface::II2c
{

public:

    /**
     * @brief   Available ports.
     */
    enum class Port : uint8_t {
        PORT_1 = 0,
        PORT_2,
        PORT_3,
        PORT_4,

        PORT_NUM    // Not a valid parameter
    };

    /**
     * @brief   Get driver instance.
     * @param   port: target port.
     * @retval  Driver instance.
     */
    static I2c& GetInstance(Port port);

    /**
     * @brief   Initialization of the I2C instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() override;

    /**
     * @brief   Deinitialization of the I2C instance.
     */
    virtual void deinit() override;


    /**
     * @brief   Transmits in master mode an amount of data.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @param   pData: Pointer to the array of data to write.
     * @param   size: Size of the data in bytes.
     * @param   blocking: 'true' blocking mode, 'false' interrupt mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool transmit(uint8_t devAddr, const uint8_t *pData,
            uint16_t size, bool blocking = true) override;

    /**
     * @brief   Receives in master mode an amount of data.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool receive(uint8_t devAddr, uint8_t *pData,
            uint16_t size, bool blocking = true) override;

    /**
     * @brief   Write an amount of data to a specific memory address/register.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @param   memAddr: Internal memory address.
     * @param   memAddrSize: Size of the memory address (1 or 2).
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be sent.
     * @param   blocking: 'true' blocking mode, 'false' interrupt mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool writeMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
            const uint8_t *pData, uint16_t size, bool blocking = true) override;

    /**
     * @brief   Read an amount of data from a specific memory address/register.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @param   memAddr: Internal memory address.
     * @param   memAddrSize: Size of the memory address (1 or 2).
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool readMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
            uint8_t *pData, uint16_t size, bool blocking = true) override;

    /**
     * @brief   Checks if target device is ready for communication.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @retval  Execution status. 'true' - device ready, 'false' - device
     *          not responded.
     */
    virtual bool isDeviceReady(uint8_t devAddr) override;

    /**
     * @brief   Reports whether the bus is currently busy (BUSY flag in
     *          the peripheral's ISR register). Useful to detect a stuck
     *          SDA held low by a slave whose power is being removed.
     * @retval  'true' - bus is busy, 'false' - bus is idle.
     */
    virtual bool isBusBusy() override;

    /**
     * @brief   Recover the peripheral via a full DeInit/Init cycle. This is
     *          the only IRQ-independent way to force the HAL state machine
     *          back to READY after a transfer was abandoned on timeout.
     * @retval  'true' - success, 'false' - otherwise.
     */
    virtual bool recover() override;

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler II2c::CpltHandler.
     */
    virtual void setCpltHandler(Interface::II2c::CpltHandler *pHandler) override
    {
        mI2cCpltHandler = pHandler;
    }

    // Interrupt handlers for this driver.
    // Call these from the matching ISRs in stm32u5xx_it.c.
    static void IrqHandlerEvtPort1();
    static void IrqHandlerErrPort1();
    static void IrqHandlerEvtPort2();
    static void IrqHandlerErrPort2();
    static void IrqHandlerEvtPort3();
    static void IrqHandlerErrPort3();
    static void IrqHandlerEvtPort4();
    static void IrqHandlerErrPort4();

private:

    // Port number
    Port mPort { };

    // HAL I2C object
    I2C_HandleTypeDef mI2cHandle { };

    // Indicates that the I2C port is initialized
    bool mIsInitialized { };

    // Set while recover() is re-initialising the peripheral, so the proactive
    // stuck-bus check in init() does not recurse back into recovery.
    bool mInRecover { };

    // Transfer timeout in ms
    static const uint32_t skTransferTimeout = 50;

    // I2C timing register values (I2CCLK = SYSCLK = 160 MHz). PORT_1 (main bus:
    // PCA9420, MAX17262, STUSB4500L, DRV2625) and PORT_2 (body bus: PAH8316LS,
    // MS5837) each carry a slave rated only for Fast Mode - the MAX17262 and
    // DRV2625 on PORT_1 and the MS5837 on PORT_2 top out at 400 kHz - so both
    // buses run at 400 kHz. 1 MHz on them violated those slaves' tLOW>=1.3us
    // timing. PORT_3 (EEPROM only) and PORT_4 (EEPROM + BMI270 + BMM350) carry
    // only Fast Mode Plus capable slaves, so they stay at 1 MHz.
    // See the port switch in I2Cx_Init().
    static const uint32_t skTimingFmPlus1MHz = 0x00701F6B;  // Fast Mode Plus 1000 kHz
    static const uint32_t skTimingFm400kHz   = 0x00F07BFF;  // Fast Mode 400 kHz

    // Bus-recovery bit-bang: a byte plus its ACK is nine SCL edges, enough
    // for a slave frozen mid-transfer to finish shifting and release SDA.
    static const uint32_t skBusRecoveryClocks       = 9;
    static const uint32_t skBusRecoveryHalfPeriodUs = 5;    // ~100 kHz

    // Transfer complete interrupt handler
    Interface::II2c::CpltHandler *mI2cCpltHandler { };

    /**
     * @brief   Constructor.
     */
    I2c() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~I2c()
    {
        deinit();
    }

    /**
     * @brief   Find instance by I2C handle.
     * @param   hi2c: HAL I2C Handle.
     * @retval  Driver instance.
     */
    static I2c& FindInstance(I2C_HandleTypeDef *i2cHandle);

    /**
     * @brief   Initialization of the I2Cx instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool I2Cx_Init(I2C_TypeDef *pInstance);

    /**
     * @brief   Deinitialization of the I2Cx instance.
     */
    void I2Cx_DeInit();

    /**
     * @brief   Disable this port's EV/ER interrupt lines in the NVIC.
     */
    void nvicDisable();

    /**
     * @brief   Resolve this port's SCL/SDA GPIO port and pin.
     */
    void busPins(GPIO_TypeDef *&sclPort, uint16_t &sclPin,
            GPIO_TypeDef *&sdaPort, uint16_t &sdaPin);

    /**
     * @brief   Read whether SDA is currently held low. Valid in either AF or
     *          GPIO mode - the input buffer is live in both.
     * @retval  'true' - SDA low, 'false' - SDA released (high).
     */
    bool isSdaHeldLow();

    /**
     * @brief   Manually clock SCL to free a slave still holding SDA low after
     *          an interrupted transfer, then issue a STOP so every slave
     *          resynchronises to bus-idle. The pins must already be released
     *          from the peripheral (call after deinit(), before init()).
     * @retval  'true' - SDA released, 'false' - SCL is stuck low
     *          (unrecoverable) or SDA never released.
     */
    bool clockOutBus();

    // HAL callbacks
    static void MspInitCallback(I2C_HandleTypeDef *i2cHandle);
    static void MspDeInitCallback(I2C_HandleTypeDef *i2cHandle);
    static void MasterCpltCallback(I2C_HandleTypeDef *i2cHandle);
    static void ErrorCallback(I2C_HandleTypeDef *i2cHandle);

};

} // namespace Driver

#endif // DRIVER_I2C_HPP
