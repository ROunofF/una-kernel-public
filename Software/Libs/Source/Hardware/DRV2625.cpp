/**
 ******************************************************************************
 * @file    DRV2625.cpp
 * @brief   Haptic Driver with Built-In Library.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "DRV2625.hpp"

#define LOG_MODULE_PRX      "DRV2625"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

#include <cassert>
#include "System.hpp"

namespace Hardware
{

DRV2625::DRV2625(Interface::II2c *pI2c, uint8_t devAddr)
{
    config(pI2c, devAddr);
}

bool DRV2625::config(Interface::II2c *pI2c, uint8_t devAddr)
{
    assert(pI2c != nullptr);

    mpI2c = pI2c;
    mAddr = devAddr;

    mIsConfigured = true;

    return true;
}

bool DRV2625::isReady()
{
    assert(mIsConfigured == true);

    return mpI2c->isDeviceReady(mAddr);
}

bool DRV2625::readReg(uint8_t reg, uint8_t &v)
{
    assert(mIsConfigured == true);

    bool ret = mpI2c->readMem(mAddr, (uint8_t) reg, 1, &v, 1);

    uint32_t retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry\n");
        ret = mpI2c->writeMem(mAddr, (uint8_t) reg, 1, &v, 1);
    }

    if (!ret) {
        LOG_ERROR("Failed to read reg(s) 0x%02X\n", reg);
    }

    return ret;
}

bool DRV2625::writeReg(uint8_t reg, uint8_t v)
{
    assert(mIsConfigured == true);

    bool ret = mpI2c->writeMem(mAddr, (uint8_t) reg, 1, &v, 1);

    uint32_t retries = skI2cRetries;
    while (!ret && retries-- > 0) {
        LOG_WARNING("Retry\n");
        ret = mpI2c->writeMem(mAddr, (uint8_t) reg, 1, &v, 1);
    }

    if (!ret) {
        LOG_ERROR("Failed to write reg(s) 0x%02X\n", reg);
    }

    return ret;
}

uint8_t DRV2625::getDeviceId()
{
    Reg00 reg { };
    readReg(0x00, reg.reg);
    return reg.CHIPID;
}

uint8_t DRV2625::getDeviceRevision()
{
    Reg00 reg { };
    readReg(0x00, reg.reg);
    return reg.REV;
}

bool DRV2625::diagnostic()
{

    setMode(Mode::DIAGNOSTICS_ROUTINE);
    go();

    uint32_t time = getTick() + 2000;
    bool done = false;
    Reg01 reg01 { };
    while (!done) {

        readReg(0x01, reg01.reg);

        if (reg01.PROCESS_DONE){
#if 0
            uint8_t data0C = 0;
            uint8_t data07 = 0;
            uint8_t data03 = 0;
            uint8_t data30 = 0;

            readReg(0x0C, data0C);
            readReg(0x07, data07);
            readReg(0x03, data03);
            readReg(0x30, data30);
            LOG_DEBUG("REG: 01=0x%02X, 0C=0x%02X, 07=0x%02X, 03=0x%02X, 30=0x%02X\n",
                    reg01.reg, data0C, data07, data03, data30);

            float resistance = 478.43 * ((float)data03 / (719 + (4 * data30)));
            LOG_DEBUG("Resistance: %.1f Ohm\n", resistance);
#endif
            break;
        }
        delay(100);  // FIXME
        if (getTick() > time) {
            return false;
        }

    }

    return reg01.DIAG_RESULT == 0; // 1 - fail
}

bool DRV2625::initERM()
{
    bool ret = setMotorType(MotorType::ERM);
    ret &= setFbBrakeFactor(3);     // A value of 3 is valid for most actuators.
    ret &= setLoopGain(2);          // A value of 2 is valid for most actuators.
    ret &= setRatedMotorVoltage(123);   // 2.7V. Default value is 63: V(ERM-CL_AV) = 1.37
    ret &= setODClamp(137);        // Default value is 137: V(ERM-OL_AV) = 2.95
    ret &= enableAutoBreak(true, false);
    return ret;
}

bool DRV2625::calibrationERM(BemfGain &bemfGain, uint8_t &calComp,
        uint8_t &calBmf)
{
    // 1. Write a value of 0x03 to the MODE[1:0] parameter in Register 0x07
    // to set the auto-calibration routine.
    if (!setMode(Mode::AUTO_LEVEL_CALIB)) {
        return false;
    }

    // 2. Populate the input parameters required by the auto-calibration engine:
    // (a) The following are input parameters required for the user to set for
    // an accurate auto-calibration
    setMotorType(MotorType::ERM);
    setRatedMotorVoltage(123);  // 2.7V. Default value is 63: V(ERM-CL_AV) = 1.37
    setODClamp(137);            // Default value is 137: V(ERM-OL_AV) = 2.95
    setDriveTime(DriveTimeUs::LRA2500ERM5000);

    // (b) The following are also input parameters but do not require
    // modification as the defaults work for the majority of actuators.
    // More info on these parameters can be found later on under optional
    // device features.
    setSampleTime(SampleTime::T_300US); // Default value of 3 is valid for most actuators.
    setFbBrakeFactor(3);     // A value of 3 is valid for most actuators.
    setAutoCalTime(AutoCalTime::T_500MS); // Default value of 3 is valid for most actuators.
    setLoopGain(2);          // A value of 2 is valid for most actuators.
    setBlankingTime(BlankingOrIdissTimeUs::LRA25ERM75); // Default value of 1 is valid for most actuators.
    setIdissTime(BlankingOrIdissTimeUs::LRA25ERM75);    // Default value of 1 is valid for most actuators.
    setZcDetTime(ZcDetTime::T_100US);   // Default value of 0 is valid for most actuators.

    // 3. Write a 1 to the GO bit in Register 0x0C to start the
    // auto-calibration process.
    if (!go()) {
        return false;
    }

    // When auto calibration is complete, the GO bit
    // automatically clears.
    uint32_t time = getTick() + 5000;
    while (getGoFlag()) {
        delay(100);  // FIXME
        if (getTick() > time) {
            return false;
        }
    }

    // 4. Check the status of the DIAG_RESULT bit in Register 0x01 to ensure
    // that the auto-calibration routine is complete without faults.
    if (getDiagResFlag()) {
        return false;
    }

    // 5 (b) Store the auto-calibration results in host processor memory and
    // rewrite them to the DRV2624 or DRV2625 device upon subsequent power
    // ups. The device retains these settings when in STANDBY mode or when
    // the EN pin is low.
    bemfGain = getFeedbackGain();
    calComp = getVoltageCompensation();
    calBmf = getFeedbackValue();

    return true;
}

bool DRV2625::applyCalibrationERM(BemfGain bemfGain, uint8_t calComp,
        uint8_t calBmf)
{
    bool ret = setFeedbackGain(bemfGain);
    ret &= setVoltageCompensation(calComp);
    ret &= setFeedbackValue(calBmf);
    return ret;
}

bool DRV2625::setSequencer(uint8_t idx, bool pause, uint8_t value, uint8_t loop)
{
    if (idx >= 8 || value > 0x7F || loop > 3) {
        return false;
    }

    // Update loop register
    uint8_t loopVal = 0;
    uint8_t loopReg = 0x17;
    uint8_t loopOffset = idx * 2;
    if (idx > 3) {
        loopReg++;
        loopOffset -= 8;
    }
    if (!readReg(loopReg, loopVal)) {
        return false;
    }

    if (((loopVal >> loopOffset) & 0x03) != loop) {
        clearBit(loopVal, (0x03 << loopOffset));
        setBit(loopVal, (loop << loopOffset));

        writeReg(loopReg, loopVal);
    }

    // Set effect
    uint8_t regEff = 0x0F + idx;
    if (pause) {
        value |= 0x80;
    }

    return writeReg(regEff, value);
}

bool DRV2625::setMainLoop(uint8_t loop)
{
    Reg19 reg { };
    reg.WAV_SEQ_MAIN_LOOP = loop;
    return writeReg(0x19, reg.reg);
}

bool DRV2625::go()
{
    Reg0C reg { };
    reg.GO = 1;
    return writeReg(0x0C, reg.reg);
}

bool DRV2625::stop()
{
    Reg0C reg { };
    reg.GO = 0;
    return writeReg(0x0C, reg.reg);
}

bool DRV2625::enableI2CBroadcast(bool en)
{
    Reg07 reg { };
    if (!readReg(0x07, reg.reg)) {
        return false;
    }
    reg.I2C_BCAST_EN = en;
    return writeReg(0x07, reg.reg);
}

bool DRV2625::enableLraPeriodAverage(bool en)
{
    Reg07 reg { };
    if (!readReg(0x07, reg.reg)) {
        return false;
    }
    reg.LRA_PERIOD_AVG_DIS = en ? 1 : 0;
    return writeReg(0x07, reg.reg);
}

bool DRV2625::enableControlLoop(bool en)
{
    Reg08 reg { };
    if (!readReg(0x08, reg.reg)) {
        return false;
    }
    reg.CONTROL_LOOP = en ? 0 : 1;
    return writeReg(0x08, reg.reg);
}

bool DRV2625::enableAutoBreak(bool en, bool openLoop)
{
    Reg08 reg { };
    if (!readReg(0x08, reg.reg)) {
        return false;
    }
    reg.AUTO_BRK_INTO_STBY = en ? 1 : 0;
    if (openLoop) {
        reg.AUTO_BRK_OL = en ? 1 : 0;
    }
    return writeReg(0x08, reg.reg);
}

bool DRV2625::enableHybridLoop(bool en)
{
    Reg08 reg { };
    if (!readReg(0x08, reg.reg)) {
        return false;
    }
    reg.HYBRID_LOOP = en ? 1 : 0;
    return writeReg(0x08, reg.reg);
}

bool DRV2625::setTriggerPinFunction(TriggerPinFunction function)
{
    Reg07 reg { };
    if (!readReg(0x07, reg.reg)) {
        return false;
    }
    reg.TRIG_PIN_FUNC = (uint8_t) function;
    return writeReg(0x07, reg.reg);
}

bool DRV2625::setMode(Mode mode)
{
    Reg07 reg { };
    if (!readReg(0x07, reg.reg)) {
        return false;
    }
    reg.MODE = (uint8_t) mode;
    return writeReg(0x07, reg.reg);
}

bool DRV2625::setMotorType(MotorType type)
{
    Reg08 reg { };
    if (!readReg(0x08, reg.reg)) {
        return false;
    }
    reg.LRA_ERM = (type == MotorType::ERM) ? 0 : 1;
    return writeReg(0x08, reg.reg);
}

bool DRV2625::setLib(MotorType type)
{
    Reg0D reg { };
    if (!readReg(0x0D, reg.reg)) {
        return false;
    }
    reg.LIB_SEL = (type == MotorType::ERM) ? 1 : 0;
    return writeReg(0x0D, reg.reg);
}

bool DRV2625::setPlaybackInterval(PlaybackInterval interval)
{
    Reg0D reg { };
    if (!readReg(0x0D, reg.reg)) {
        return false;
    }
    reg.PLAYBACK_INTERVAL = (uint8_t) interval;
    return writeReg(0x0D, reg.reg);
}

bool DRV2625::setDigMemGain(DigMemGain gain)
{
    Reg0D reg { };
    if (!readReg(0x0D, reg.reg)) {
        return false;
    }
    reg.PLAYBACK_INTERVAL = (uint8_t) gain;
    return writeReg(0x0D, reg.reg);
}

bool DRV2625::setRealTimeAmplitude(uint8_t amplitude)
{
    return writeReg(0x0E, amplitude);
}

bool DRV2625::setOverdriveOpenLoop(uint8_t value)
{
    return writeReg(0x1A, value);
}

bool DRV2625::setRatedMotorVoltage(uint8_t value)
{
    return writeReg(0x1F, value);
}

bool DRV2625::setODClamp(uint8_t value)
{
    return writeReg(0x20, value);
}

bool DRV2625::setAutoCalTime(AutoCalTime time)
{
    Reg2A reg { };
    if (!readReg(0x2A, reg.reg)) {
        return false;
    }
    reg.AUTO_CAL_TIME = (uint8_t) time;
    return writeReg(0x2A, reg.reg);
}

bool DRV2625::setBlankingTime(BlankingOrIdissTimeUs time)
{
    Reg28 reg { };
    if (!readReg(0x28, reg.reg)) {
        return false;
    }
    reg.BLANKING_TIME = (uint8_t) time;
    return writeReg(0x28, reg.reg);
}

bool DRV2625::setIdissTime(BlankingOrIdissTimeUs time)
{
    Reg28 reg { };
    if (!readReg(0x28, reg.reg)) {
        return false;
    }
    reg.IDISS_TIME = (uint8_t) time;
    return writeReg(0x28, reg.reg);
}

bool DRV2625::setFbBrakeFactor(uint8_t value)
{
    Reg23 reg { };
    if (!readReg(0x23, reg.reg)) {
        return false;
    }
    reg.FB_BRAKE_FACTOR = value;
    return writeReg(0x23, reg.reg);
}

bool DRV2625::setLoopGain(uint8_t value)
{
    Reg23 reg { };
    if (!readReg(0x23, reg.reg)) {
        return false;
    }
    reg.LOOP_GAIN = value;
    return writeReg(0x23, reg.reg);
}

bool DRV2625::setLraMinFrequency(MinFreq freq)
{
    Reg27 reg { };
    if (!readReg(0x27, reg.reg)) {
        return false;
    }
    reg.LRA_MIN_FREQ_SEL = (uint8_t) freq;
    return writeReg(0x27, reg.reg);
}

bool DRV2625::setDriveTime(DriveTimeUs value)
{
    Reg27 reg { };
    uint8_t regNum = 0x27;
    if (!readReg(regNum, reg.reg)) {
        return false;
    }
    reg.DRIVE_TIME = (uint8_t) value;
    return writeReg(regNum, reg.reg);
}

bool DRV2625::setLraWaveShape(WaveShape wave)
{
    Reg2C reg { };
    uint8_t regNum = 0x2C;
    if (!readReg(regNum, reg.reg)) {
        return false;
    }
    reg.LRA_WAVE_SHAPE = (uint8_t) wave;
    return writeReg(regNum, reg.reg);
}

bool DRV2625::setSampleTime(SampleTime time)
{
    Reg29 reg { };
    uint8_t regNum = 0x29;
    if (!readReg(regNum, reg.reg)) {
        return false;
    }
    reg.SAMPLE_TIME = (uint8_t) time;
    return writeReg(regNum, reg.reg);
}

bool DRV2625::setZcDetTime(ZcDetTime time)
{
    Reg29 reg { };
    uint8_t regNum = 0x29;
    if (!readReg(regNum, reg.reg)) {
        return false;
    }
    reg.ZC_DET_TIME = (uint8_t) time;
    return writeReg(regNum, reg.reg);
}

bool DRV2625::getDiagResFlag()
{
    Reg01 reg { };
    uint8_t regNum = 0x01;
    readReg(regNum, reg.reg);
    return reg.DIAG_RESULT;
}

bool DRV2625::getProcessDoneFlag()
{
    Reg01 reg { };
    uint8_t regNum = 0x01;
    readReg(regNum, reg.reg);
    return reg.PROCESS_DONE;
}

bool DRV2625::getUVLOFlag()
{
    Reg01 reg { };
    uint8_t regNum = 0x01;
    readReg(regNum, reg.reg);
    return reg.UVLO;
}

bool DRV2625::getOverTempFlag()
{
    Reg01 reg { };
    uint8_t regNum = 0x01;
    readReg(regNum, reg.reg);
    return reg.OVER_TEMP;
}

bool DRV2625::getOCDetectFlag()
{
    Reg01 reg { };
    uint8_t regNum = 0x01;
    readReg(regNum, reg.reg);
    return reg.OC_DETECT;
}

uint8_t DRV2625::getStatusFlags()
{
    Reg01 reg { };
    uint8_t regNum = 0x01;
    readReg(regNum, reg.reg);
    return reg.reg;
}

uint8_t DRV2625::getDiagZResult()
{
    uint8_t v = 0;
    readReg(0x03, v);
    return v;
}

bool DRV2625::getGoFlag()
{
    Reg0C reg { };
    uint8_t regNum = 0x0C;
    readReg(regNum, reg.reg);
    return reg.GO;
}

uint8_t DRV2625::getVoltageCompensation()
{
    uint8_t v = 0;
    readReg(0x21, v);
    return v;
}

bool DRV2625::setVoltageCompensation(uint8_t value)
{
    return writeReg(0x21, value);
}

uint8_t DRV2625::getFeedbackValue()
{
    uint8_t v = 0;
    readReg(0x22, v);
    return v;
}

bool DRV2625::setFeedbackValue(uint8_t value)
{
    return writeReg(0x22, value);
}

DRV2625::BemfGain DRV2625::getFeedbackGain()
{
    Reg23 reg { };
    uint8_t regNum = 0x23;
    readReg(regNum, reg.reg);
    return (BemfGain) reg.BEMF_GAIN;
}

bool DRV2625::setFeedbackGain(BemfGain gain)
{
    Reg23 reg { };
    uint8_t regNum = 0x23;
    if (!readReg(regNum, reg.reg)) {
        return false;
    }
    reg.BEMF_GAIN = (uint8_t) gain;
    return writeReg(regNum, reg.reg);
}

void DRV2625::setBit(uint8_t &reg, uint8_t mask)
{
    reg |= mask;
}

void DRV2625::clearBit(uint8_t &reg, uint8_t mask)
{
    reg &= ~mask;
}

uint32_t DRV2625::getTick()
{
    return Driver::System::GetTimeMs();
}

void DRV2625::delay(uint32_t ms)
{
    Driver::System::Delay(ms);
}

bool DRV2625::waitDone(uint32_t timeout)
{
    uint32_t time = getTick() + timeout;
    bool done = false;

    while (!done) {
        done = getProcessDoneFlag();

        if (getTick() > time) {
            return false;
        }
    }

    return true;
}

} /* namespace Hardware */
