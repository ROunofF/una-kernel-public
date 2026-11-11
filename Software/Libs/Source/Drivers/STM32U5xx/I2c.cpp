/**
 ******************************************************************************
 * @file    I2c.cpp
 * @brief   I2C driver.
 ******************************************************************************
 *
 ******************************************************************************
 */


#include "I2c.hpp"

#define LOG_MODULE_PRX      "I2c"
#define LOG_MODULE_LEVEL    LOG_LEVEL_WARNING
#include "Logger.h"

#include <cassert>

#include "UsTimer.hpp"

namespace Driver
{

// NOTE!
// Sometimes I2C takes more than 35ms to complete an operation. Therefore, the timeout is 50ms.

#define LOG_HAL_ERR_MSG(port, msg, err) \
        LOG_ERROR("PORT_%u: %s failed. HAL Err %d\n", static_cast<uint8_t>(port) + 1, msg, err);

// For some devices a NACK is part of the protocol and is not an error.
// So, we display a NACK only in LOG_LEVEL_DEBUG.
#define LOG_I2C_ERR_MSG(port, msg, err) \
    do {    \
        uint32_t i2cErr = HAL_I2C_GetError(&GetInstance(port).mI2cHandle); \
        if (LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG || i2cErr != HAL_I2C_ERROR_AF) \
        { \
        LOG_ERROR("PORT_%u: %s failed. HAL Err %d, I2C err %d\n", static_cast<uint8_t>(port) + 1, msg, err, i2cErr); \
        } \
    } while (0)

I2c& I2c::GetInstance(I2c::Port port)
{
    assert(port < I2c::Port::PORT_NUM);

    uint8_t n = static_cast<uint8_t>(port);
    static I2c sInstance[static_cast<uint32_t>(Port::PORT_NUM)];
    // Set the port once. Avoids a repeated write to shared state that is
    // also touched from ISR context (IRQ handlers, error log macro).
    if (sInstance[n].mPort != port) {
        sInstance[n].mPort = port;
    }
    return sInstance[n];
}

bool I2c::init()
{
    bool ok = false;

    switch (mPort) {
        case Port::PORT_1:
            ok = I2Cx_Init(I2C1);
            break;
        case Port::PORT_2:
            ok = I2Cx_Init(I2C2);
            break;
        case Port::PORT_3:
            ok = I2Cx_Init(I2C3);
            break;
        case Port::PORT_4:
            ok = I2Cx_Init(I2C4);
            break;

        default:
            assert(false);
            return false;
    }

    // A reset (e.g. a reflash) can interrupt a transfer before we own the bus,
    // leaving a slave holding SDA low. Clock it free once, up front, so the
    // first real transfer does not have to fail into reactive recovery. The
    // mInRecover guard stops this recursing: recover() re-inits the peripheral.
    if (ok && !mInRecover && isSdaHeldLow()) {
        LOG_WARNING("PORT_%u: SDA held low at init, recovering bus\n",
                static_cast<uint8_t>(mPort) + 1);
        recover();
    }

    return ok;
}

void I2c::deinit()
{
    I2Cx_DeInit();
}

bool I2c::transmit(uint8_t devAddr, const uint8_t *pData,
        uint16_t size, bool blocking)
{
    assert(pData != nullptr);
    HAL_StatusTypeDef err = HAL_ERROR;

    if (!mIsInitialized){
        return false;
    }

    if (blocking) {
        err = HAL_I2C_Master_Transmit(&mI2cHandle, devAddr << 1,
                (uint8_t*) pData, size, skTransferTimeout);
        if (err != HAL_OK) {
            LOG_I2C_ERR_MSG(mPort, "HAL_I2C_Master_Transmit", err);
        }
    } else {
        err = HAL_I2C_Master_Transmit_IT(&mI2cHandle, devAddr << 1,
                (uint8_t*) pData, size);
        if (err != HAL_OK) {
            LOG_I2C_ERR_MSG(mPort, "HAL_I2C_Master_Transmit_IT", err);
        }
    }

    return err == HAL_OK;
}

bool I2c::receive(uint8_t devAddr, uint8_t *pData,
        uint16_t size, bool blocking)
{
    assert(pData != nullptr);
    HAL_StatusTypeDef err = HAL_ERROR;

    if (!mIsInitialized){
        return false;
    }

    if (blocking) {
        err = HAL_I2C_Master_Receive(&mI2cHandle, devAddr << 1,
                (uint8_t*) pData, size, skTransferTimeout);
        if (err != HAL_OK) {
            LOG_I2C_ERR_MSG(mPort, "HAL_I2C_Master_Receive", err);
        }
    } else {
        err = HAL_I2C_Master_Receive_IT(&mI2cHandle, devAddr << 1,
                (uint8_t*) pData, size);
        if (err != HAL_OK) {
            LOG_I2C_ERR_MSG(mPort, "HAL_I2C_Master_Receive_IT", err);
        }
    }

    return err == HAL_OK;
}

bool I2c::writeMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
        const uint8_t *pData, uint16_t size, bool blocking)
{
    assert(pData != nullptr);
    assert(memAddrSize == 1 || memAddrSize == 2);
    HAL_StatusTypeDef err = HAL_ERROR;

    if (!mIsInitialized){
        return false;
    }

    if (blocking) {
        uint32_t timeout = ((size / 10) + 1) * 2;
        if (timeout < skTransferTimeout) {
            timeout = skTransferTimeout;
        }
        err = HAL_I2C_Mem_Write(&mI2cHandle, devAddr << 1, memAddr,
                memAddrSize == 1 ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT,
                (uint8_t*) pData, size, timeout);
        if (err != HAL_OK) {
            LOG_I2C_ERR_MSG(mPort, "HAL_I2C_Mem_Write", err);
        }
    } else {
        err = HAL_I2C_Mem_Write_IT(&mI2cHandle, devAddr << 1, memAddr,
                memAddrSize == 1 ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT,
                (uint8_t*) pData, size);
        if (err != HAL_OK) {
            LOG_I2C_ERR_MSG(mPort, "HAL_I2C_Mem_Write_IT", err);
        }
    }

    return err == HAL_OK;
}

bool I2c::readMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
        uint8_t *pData, uint16_t size, bool blocking)
{
    assert(pData != nullptr);
    assert(memAddrSize == 1 || memAddrSize == 2);
    HAL_StatusTypeDef err = HAL_ERROR;

    if (!mIsInitialized){
        return false;
    }

    if (blocking) {
        uint32_t timeout = ((size / 10) + 1) * 2;
        if (timeout < skTransferTimeout) {
            timeout = skTransferTimeout;
        }
        err = HAL_I2C_Mem_Read(&mI2cHandle, devAddr << 1, memAddr,
                memAddrSize == 1 ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT,
                (uint8_t*) pData, size, timeout);
        if (err != HAL_OK) {
            LOG_I2C_ERR_MSG(mPort, "HAL_I2C_Mem_Read", err);
        }
    } else {
        err = HAL_I2C_Mem_Read_IT(&mI2cHandle, devAddr << 1, memAddr,
                memAddrSize == 1 ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT,
                (uint8_t*) pData, size);
        if (err != HAL_OK) {
            LOG_I2C_ERR_MSG(mPort, "HAL_I2C_Mem_Read_IT", err);
        }
    }

    return err == HAL_OK;
}

bool I2c::isDeviceReady(uint8_t devAddr)
{
    HAL_StatusTypeDef err = HAL_ERROR;

    if (!mIsInitialized){
        return false;
    }

    err = HAL_I2C_IsDeviceReady(&mI2cHandle, devAddr << 1, 10, skTransferTimeout);

    return err == HAL_OK;
}

bool I2c::isBusBusy()
{
    if (!mIsInitialized) {
        return false;
    }

    return __HAL_I2C_GET_FLAG(&mI2cHandle, I2C_FLAG_BUSY) != 0;
}

bool I2c::recover()
{
    // Nothing to recover on a bus that is intentionally down. Do not power
    // it back up as a side effect of a stray failed transfer.
    if (!mIsInitialized) {
        return false;
    }

    // Mask the port interrupts up front so a late completion IRQ from the
    // abandoned transfer cannot run the HAL handler while DeInit/Init tears
    // the handle down. MspInit re-enables them on init().
    nvicDisable();

    // A DeInit/Init forces hi2c->State back to READY, but it only resets the
    // master - it never drives SCL. A slave frozen mid-transfer keeps holding
    // SDA low, so every later start loses arbitration. DeInit first to free
    // the pins, then clock the slave out by hand before bringing the
    // peripheral back up.
    deinit();
    clockOutBus();

    // init() runs a proactive stuck-bus check; suppress it here so a bus we
    // could not free does not recurse endlessly through recover().
    mInRecover = true;
    bool ok = init();
    mInRecover = false;
    return ok;
}

void I2c::busPins(GPIO_TypeDef *&sclPort, uint16_t &sclPin,
        GPIO_TypeDef *&sdaPort, uint16_t &sdaPin)
{
    switch (mPort) {
        case Port::PORT_1:
            sclPort = MAIN_SCL_GPIO_Port;       sclPin = MAIN_SCL_Pin;
            sdaPort = MAIN_SDA_GPIO_Port;       sdaPin = MAIN_SDA_Pin;
            break;
        case Port::PORT_2:
            sclPort = SENSORBODY_SCL_GPIO_Port; sclPin = SENSORBODY_SCL_Pin;
            sdaPort = SENSORBODY_SDA_GPIO_Port; sdaPin = SENSORBODY_SDA_Pin;
            break;
        case Port::PORT_3:
            sclPort = SENSOR1_SCL_GPIO_Port;    sclPin = SENSOR1_SCL_Pin;
            sdaPort = SENSOR1_SDA_GPIO_Port;    sdaPin = SENSOR1_SDA_Pin;
            break;
        case Port::PORT_4:
            sclPort = SENSOR2_SCL_GPIO_Port;    sclPin = SENSOR2_SCL_Pin;
            sdaPort = SENSOR2_SDA_GPIO_Port;    sdaPin = SENSOR2_SDA_Pin;
            break;
        default:
            sclPort = nullptr; sclPin = 0;
            sdaPort = nullptr; sdaPin = 0;
            break;
    }
}

bool I2c::isSdaHeldLow()
{
    GPIO_TypeDef *sclPort = nullptr;
    GPIO_TypeDef *sdaPort = nullptr;
    uint16_t      sclPin  = 0;
    uint16_t      sdaPin  = 0;

    busPins(sclPort, sclPin, sdaPort, sdaPin);
    if (sdaPort == nullptr) {
        return false;
    }
    return HAL_GPIO_ReadPin(sdaPort, sdaPin) == GPIO_PIN_RESET;
}

bool I2c::clockOutBus()
{
    GPIO_TypeDef *sclPort = nullptr;
    GPIO_TypeDef *sdaPort = nullptr;
    uint16_t      sclPin  = 0;
    uint16_t      sdaPin  = 0;

    busPins(sclPort, sclPin, sdaPort, sdaPin);
    if (sclPort == nullptr) {
        return false;
    }

    // The GPIO bank clock stays on across a peripheral deinit, but enable it
    // defensively so driving the pins cannot silently no-op.
    switch (mPort) {
        case Port::PORT_1:
        case Port::PORT_2: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
        case Port::PORT_3: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
        case Port::PORT_4: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
        default: break;
    }

    // DelayUs busy-waits on the DWT cycle counter; make sure it is running so
    // the loop below cannot spin forever on a stopped counter.
    UsTimer::Init();

    // Preset both lines released (high) before enabling the output drivers so
    // the switch to open-drain does not glitch the bus low. The bus relies on
    // external pull-ups - releasing a line means letting it float high.
    HAL_GPIO_WritePin(sclPort, sclPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(sdaPort, sdaPin, GPIO_PIN_SET);

    GPIO_InitTypeDef gpio = { 0 };
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin   = sclPin;
    HAL_GPIO_Init(sclPort, &gpio);
    gpio.Pin   = sdaPin;
    HAL_GPIO_Init(sdaPort, &gpio);
    UsTimer::DelayUs(skBusRecoveryHalfPeriodUs);

    // If SCL will not float high a slave is stretching the clock or the line
    // is shorted; the master cannot recover that.
    if (HAL_GPIO_ReadPin(sclPort, sclPin) == GPIO_PIN_RESET) {
        LOG_ERROR("PORT_%u: SCL stuck low, cannot recover bus\n",
                static_cast<uint8_t>(mPort) + 1);
        return false;
    }

    // A slave frozen mid-transfer is waiting to finish clocking out the byte
    // it was driving. It releases SDA only when it reaches the ACK slot, so we
    // must issue a whole byte + ACK - nine edges - to guarantee it lets go,
    // whatever bit it stalled on. We deliberately do NOT stop early on SDA
    // reading high: a data '1' also floats the line, so a mid-byte SDA read
    // cannot tell "released" from "sending a 1". Nine clocks always clears the
    // frame; any edges past the release land on an idle line and are harmless.
    for (uint32_t i = 0; i < skBusRecoveryClocks; i++) {
        HAL_GPIO_WritePin(sclPort, sclPin, GPIO_PIN_RESET);
        UsTimer::DelayUs(skBusRecoveryHalfPeriodUs);
        HAL_GPIO_WritePin(sclPort, sclPin, GPIO_PIN_SET);
        UsTimer::DelayUs(skBusRecoveryHalfPeriodUs);
    }

    // Sample the outcome now, while SCL is high and before the STOP drives
    // SDA, to see whether the slave actually let go.
    bool released = HAL_GPIO_ReadPin(sdaPort, sdaPin) == GPIO_PIN_SET;

    // Issue a STOP (SDA low->high while SCL high) so every slave resynchronises
    // its state machine to bus-idle regardless of where it stalled.
    HAL_GPIO_WritePin(sclPort, sclPin, GPIO_PIN_RESET);
    UsTimer::DelayUs(skBusRecoveryHalfPeriodUs);
    HAL_GPIO_WritePin(sdaPort, sdaPin, GPIO_PIN_RESET);
    UsTimer::DelayUs(skBusRecoveryHalfPeriodUs);
    HAL_GPIO_WritePin(sclPort, sclPin, GPIO_PIN_SET);
    UsTimer::DelayUs(skBusRecoveryHalfPeriodUs);
    HAL_GPIO_WritePin(sdaPort, sdaPin, GPIO_PIN_SET);
    UsTimer::DelayUs(skBusRecoveryHalfPeriodUs);

    if (!released) {
        LOG_WARNING("PORT_%u: SDA still low after %u clocks\n",
                static_cast<uint8_t>(mPort) + 1, skBusRecoveryClocks);
    }

    return released;
}

void I2c::nvicDisable()
{
    switch (mPort) {
        case Port::PORT_1:
            HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
            HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
            break;
        case Port::PORT_2:
            HAL_NVIC_DisableIRQ(I2C2_EV_IRQn);
            HAL_NVIC_DisableIRQ(I2C2_ER_IRQn);
            break;
        case Port::PORT_3:
            HAL_NVIC_DisableIRQ(I2C3_EV_IRQn);
            HAL_NVIC_DisableIRQ(I2C3_ER_IRQn);
            break;
        case Port::PORT_4:
            HAL_NVIC_DisableIRQ(I2C4_EV_IRQn);
            HAL_NVIC_DisableIRQ(I2C4_ER_IRQn);
            break;
        default:
            break;
    }
}

void I2c::IrqHandlerEvtPort1()
{
    HAL_I2C_EV_IRQHandler(&GetInstance(Port::PORT_1).mI2cHandle);
}

void I2c::IrqHandlerErrPort1()
{
    HAL_I2C_ER_IRQHandler(&GetInstance(Port::PORT_1).mI2cHandle);
}

void I2c::IrqHandlerEvtPort2()
{
    HAL_I2C_EV_IRQHandler(&GetInstance(Port::PORT_2).mI2cHandle);
}

void I2c::IrqHandlerErrPort2()
{
    HAL_I2C_ER_IRQHandler(&GetInstance(Port::PORT_2).mI2cHandle);
}

void I2c::IrqHandlerEvtPort3()
{
    HAL_I2C_EV_IRQHandler(&GetInstance(Port::PORT_3).mI2cHandle);
}

void I2c::IrqHandlerErrPort3()
{
    HAL_I2C_ER_IRQHandler(&GetInstance(Port::PORT_3).mI2cHandle);
}

void I2c::IrqHandlerEvtPort4()
{
    HAL_I2C_EV_IRQHandler(&GetInstance(Port::PORT_4).mI2cHandle);
}

void I2c::IrqHandlerErrPort4()
{
    HAL_I2C_ER_IRQHandler(&GetInstance(Port::PORT_4).mI2cHandle);
}

I2c& I2c::FindInstance(I2C_HandleTypeDef *i2cHandle)
{
    for (uint8_t i = 0; i < static_cast<uint8_t>(Port::PORT_NUM); i++) {
        I2c &instance = GetInstance(static_cast<Port>(i));
        if (&instance.mI2cHandle == i2cHandle) {
            return instance;
        }
    }
    assert(false);
    // Should never happen. Prevent compiler warning
    return GetInstance(Port::PORT_1);
}

bool I2c::I2Cx_Init(I2C_TypeDef *pInstance)
{
    if (mIsInitialized) {
        return true;
    }

    HAL_StatusTypeDef err = HAL_ERROR;

    mI2cHandle.Instance = pInstance;

    // Per-port SCL speed. PORT_1 and PORT_2 host slaves rated only for Fast
    // Mode (400 kHz), so they run at 400 kHz; PORT_3/4 carry only Fast Mode
    // Plus capable slaves and stay at 1 MHz. See skTiming* in I2c.hpp.
    switch (mPort) {
        case Port::PORT_1:
        case Port::PORT_2:
            mI2cHandle.Init.Timing = skTimingFm400kHz;      // Fast Mode 400 kHz
            break;
        default:
            mI2cHandle.Init.Timing = skTimingFmPlus1MHz;    // Fast Mode Plus 1 MHz
            break;
    }
    mI2cHandle.Init.OwnAddress1 = 0;
    mI2cHandle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    mI2cHandle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    mI2cHandle.Init.OwnAddress2 = 0;
    mI2cHandle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    mI2cHandle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    mI2cHandle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    mI2cHandle.MspInitCallback = MspInitCallback;
    mI2cHandle.MspDeInitCallback = MspDeInitCallback;

    err = HAL_I2C_Init(&mI2cHandle);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_I2C_Init", err);
        return false;
    }

    // Configure Analogue filter
    err = HAL_I2CEx_ConfigAnalogFilter(&mI2cHandle, I2C_ANALOGFILTER_ENABLE);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_I2CEx_ConfigAnalogFilter", err);
        return false;
    }

    // Configure Digital filter
    err = HAL_I2CEx_ConfigDigitalFilter(&mI2cHandle, 0);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_I2CEx_ConfigDigitalFilter", err);
        return false;
    }

    mI2cHandle.MasterTxCpltCallback = MasterCpltCallback;
    mI2cHandle.MasterRxCpltCallback = MasterCpltCallback;
    mI2cHandle.MemTxCpltCallback = MasterCpltCallback;
    mI2cHandle.MemRxCpltCallback = MasterCpltCallback;
    mI2cHandle.ErrorCallback = ErrorCallback;

    mIsInitialized = true;
    return true;
}

void I2c::I2Cx_DeInit()
{
    if (!mIsInitialized){
        return;
    }

    HAL_I2C_DeInit(&mI2cHandle);
    mIsInitialized = false;
}

void I2c::MspInitCallback(I2C_HandleTypeDef *i2cHandle)
{
    HAL_StatusTypeDef err = HAL_ERROR;
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };
    RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };

    uint32_t pinsSpeed = GPIO_SPEED_FREQ_LOW;

    if (i2cHandle->Instance == I2C1) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
        PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_SYSCLK;
        err = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_RCCEx_PeriphCLKConfig", err);
        }

        __HAL_RCC_GPIOB_CLK_ENABLE();
        // I2C1 GPIO Configuration
        // PB6     ------> I2C1_SCL
        // PB7     ------> I2C1_SDA
        GPIO_InitStruct.Pin = MAIN_SCL_Pin | MAIN_SDA_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = pinsSpeed;
        GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        // I2C1 clock enable
        __HAL_RCC_I2C1_CLK_ENABLE();

        // I2C1 interrupt Init
        HAL_NVIC_SetPriority(I2C1_EV_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_SetPriority(I2C1_ER_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    } else if (i2cHandle->Instance == I2C2) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C2;
        PeriphClkInit.I2c2ClockSelection = RCC_I2C2CLKSOURCE_SYSCLK;
        err = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_RCCEx_PeriphCLKConfig", err);
        }

        __HAL_RCC_GPIOB_CLK_ENABLE();
        // I2C2 GPIO Configuration
        // PB13     ------> I2C2_SCL
        // PB14     ------> I2C2_SDA

        GPIO_InitStruct.Pin = SENSORBODY_SCL_Pin | SENSORBODY_SDA_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = pinsSpeed;
        GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        // I2C2 clock enable
        __HAL_RCC_I2C2_CLK_ENABLE();

        // I2C2 interrupt Init
        HAL_NVIC_SetPriority(I2C2_EV_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C2_EV_IRQn);
        HAL_NVIC_SetPriority(I2C2_ER_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C2_ER_IRQn);
    } else if (i2cHandle->Instance == I2C3) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C3;
        PeriphClkInit.I2c3ClockSelection = RCC_I2C3CLKSOURCE_SYSCLK;
        err = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_3, "HAL_RCCEx_PeriphCLKConfig", err);
        }

        __HAL_RCC_GPIOC_CLK_ENABLE();
        // I2C3 GPIO Configuration
        // PC0     ------> I2C3_SCL
        // PC1     ------> I2C3_SDA
        GPIO_InitStruct.Pin = SENSOR1_SCL_Pin | SENSOR1_SDA_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = pinsSpeed;
        GPIO_InitStruct.Alternate = GPIO_AF4_I2C3;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        // I2C3 clock enable
        __HAL_RCC_I2C3_CLK_ENABLE();

        // I2C3 interrupt Init
        HAL_NVIC_SetPriority(I2C3_EV_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C3_EV_IRQn);
        HAL_NVIC_SetPriority(I2C3_ER_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C3_ER_IRQn);
    } else if (i2cHandle->Instance == I2C4) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
        PeriphClkInit.I2c4ClockSelection = RCC_I2C4CLKSOURCE_SYSCLK;
        err = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_4, "HAL_RCCEx_PeriphCLKConfig", err);
        }

        __HAL_RCC_GPIOD_CLK_ENABLE();
        // I2C4 GPIO Configuration
        // PD12     ------> I2C4_SCL
        // PD13     ------> I2C4_SDA
        GPIO_InitStruct.Pin = SENSOR2_SCL_Pin | SENSOR2_SDA_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = pinsSpeed;
        GPIO_InitStruct.Alternate = GPIO_AF4_I2C4;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

        // I2C4 clock enable
        __HAL_RCC_I2C4_CLK_ENABLE();

        // I2C4 interrupt Init
        HAL_NVIC_SetPriority(I2C4_EV_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C4_EV_IRQn);
        HAL_NVIC_SetPriority(I2C4_ER_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(I2C4_ER_IRQn);
    }
}

void I2c::MspDeInitCallback(I2C_HandleTypeDef *i2cHandle)
{
    if (i2cHandle->Instance == I2C1) {
        // Peripheral clock disable
        __HAL_RCC_I2C1_CLK_DISABLE();

        // I2C1 GPIO Configuration
        // PB6     ------> I2C1_SCL
        // PB7     ------> I2C1_SDA
        HAL_GPIO_DeInit(MAIN_SCL_GPIO_Port, MAIN_SCL_Pin);
        HAL_GPIO_DeInit(MAIN_SDA_GPIO_Port, MAIN_SDA_Pin);

        // I2C1 interrupt Deinit
        HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
    } else if (i2cHandle->Instance == I2C2) {
        // Peripheral clock disable
        __HAL_RCC_I2C2_CLK_DISABLE();

        // I2C2 GPIO Configuration
        // PB13     ------> I2C2_SCL
        // PB14     ------> I2C2_SDA
        HAL_GPIO_DeInit(SENSORBODY_SCL_GPIO_Port, SENSORBODY_SCL_Pin);
        HAL_GPIO_DeInit(SENSORBODY_SDA_GPIO_Port, SENSORBODY_SDA_Pin);

        // I2C2 interrupt Deinit
        HAL_NVIC_DisableIRQ(I2C2_EV_IRQn);
        HAL_NVIC_DisableIRQ(I2C2_ER_IRQn);
    } else if (i2cHandle->Instance == I2C3) {
        // Peripheral clock disable
        __HAL_RCC_I2C3_CLK_DISABLE();

        // I2C3 GPIO Configuration
        // PC0     ------> I2C3_SCL
        // PC1     ------> I2C3_SDA
        HAL_GPIO_DeInit(SENSOR1_SCL_GPIO_Port, SENSOR1_SCL_Pin);
        HAL_GPIO_DeInit(SENSOR1_SDA_GPIO_Port, SENSOR1_SDA_Pin);

        // I2C3 interrupt Deinit
        HAL_NVIC_DisableIRQ(I2C3_EV_IRQn);
        HAL_NVIC_DisableIRQ(I2C3_ER_IRQn);
    } else if (i2cHandle->Instance == I2C4) {
        // Peripheral clock disable
        __HAL_RCC_I2C4_CLK_DISABLE();

        // I2C4 GPIO Configuration
        // PD12     ------> I2C4_SCL
        // PD13     ------> I2C4_SDA
        HAL_GPIO_DeInit(SENSOR2_SCL_GPIO_Port, SENSOR2_SCL_Pin);
        HAL_GPIO_DeInit(SENSOR2_SDA_GPIO_Port, SENSOR2_SDA_Pin);

        // I2C4 interrupt Deinit
        HAL_NVIC_DisableIRQ(I2C4_EV_IRQn);
        HAL_NVIC_DisableIRQ(I2C4_ER_IRQn);
    }
}

void I2c::MasterCpltCallback(I2C_HandleTypeDef *i2cHandle)
{
    I2c &instance = FindInstance(i2cHandle);
    if (instance.mI2cCpltHandler != nullptr) {
        instance.mI2cCpltHandler->transferCplt(&instance, true);
    }
}

void I2c::ErrorCallback(I2C_HandleTypeDef *i2cHandle)
{
    // A NACK (AF) is part of the protocol for some devices, so do not log it
    // as an error - same policy as LOG_I2C_ERR_MSG. The transfer is still
    // reported as failed below.
    if (i2cHandle->ErrorCode != HAL_I2C_ERROR_AF) {
        LOG_ERROR("ErrorCode 0x%08X\n", i2cHandle->ErrorCode);
    }
    I2c &instance = FindInstance(i2cHandle);
    if (instance.mI2cCpltHandler != nullptr) {
        instance.mI2cCpltHandler->transferCplt(&instance, false);
    }
}

} // namespace Driver

