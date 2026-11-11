/**
 ******************************************************************************
 * @file    Vibro.cpp
 * @brief   Vibro Driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Vibro.hpp"

#define LOG_MODULE_PRX      "Vibro"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

#include <cassert>

#include "main.h"
#include "Gpio.hpp"
#include "I2c.hpp"

#if (VIBRO_USE_SETTINGS_IN_RTC == 1)
#include "Rtc.hpp"
    /**
     * @brief  Structure to save config like a double-word
     */
    struct VibroConfigBackupValue {
        union {
            uint32_t raw;
            struct {
                uint8_t bemfGain;
                uint8_t calComp;
                uint8_t calBmf;
                uint8_t reserved;
            };
        };
    };
    /// Backup register number
    static constexpr uint32_t skConfigBkpReg = RTC_BRK_REG_VIBRO_CFG;
#endif

bool Vibro::init()
{
    assert(mpRst != nullptr);

    if (mIsInit) {
        return true;
    }

    mpRst->init(Interface::IGpo::Type::PP, false);

    mMutex.init(LOG_MODULE_PRX);

    // Test chip
    if (mMutex.lock()) {

        mpRst->set(true);
        OS::Delay(2);

        if (mDRV2625.isReady()) {
            // Check Revision
            uint8_t id = mDRV2625.getDeviceId();
            uint8_t rev = mDRV2625.getDeviceRevision();
            LOG_DEBUG("DRV2625 ID: %d, REV: %d\n", id, rev);

            if (id == Hardware::DRV2625::skChipID && rev
                    == Hardware::DRV2625::skRevision)
            {

                // Write the MODE[1:0] parameter in Register 0x07 to value 0x00
                // to remove the device from standby
                mDRV2625.setMode(Hardware::DRV2625::Mode::REAL_TIME);

                // Use GO bit instead of pin
                mDRV2625.setTriggerPinFunction(
                        Hardware::DRV2625::TriggerPinFunction::EXT_PULSE);

                mDRV2625.setMotorType(Hardware::DRV2625::MotorType::ERM);

                // Diagnostic
                mIsInit = mDRV2625.diagnostic();
                if (!mIsInit) {
                    LOG_ERROR("DRV2625 diagnostic failed\n");
                } else {
                    LOG_DEBUG("DRV2625 diagnostic OK\n");
                }
            } else {
                LOG_ERROR("DRV2625 wrong id/rev: %d/%d\n", id, rev);
            }
        } else {
            LOG_ERROR("DRV2625 not found\n");
        }

        mpRst->set(false);  // shutdown

        mMutex.release();
    }

    LOG_INFO("Initialized\n");
    return mIsInit;
}

bool Vibro::deinit()
{
    if (!mIsInit) {
        return true;
    }
    if (mMutex.lock()) {
        mpRst->set(false);  // shutdown
        mpRst->deinit();
        mIsInit = false;
        mMutex.release();
    }
    return true;
}

bool Vibro::enable()
{
    if (!mIsInit) {
        return false;
    }

    if (mIsOn) {
        return true;
    }

    if (!mMutex.lock()) {
        return false;
    }

    bool status = false;

    uint8_t bemfGain = 0;
    uint8_t calComp = 0;
    uint8_t calBmf = 0;

    mpRst->set(true);
    OS::Delay(2);

    // Write the MODE[1:0] parameter in Register 0x07 to value 0x00
    // to remove the device from standby
    status = mDRV2625.setMode(Hardware::DRV2625::Mode::REAL_TIME);
    if (status == false) {
        LOG_ERROR("setMode() failed\n");
        goto exit;
    }

    // Use GO bit instead of pin
    status = mDRV2625.setTriggerPinFunction(
            Hardware::DRV2625::TriggerPinFunction::EXT_PULSE);
    if (status == false) {
        LOG_ERROR("setTriggerPinFunction() failed\n");
        goto exit;
    }

    // Init with base configuration
    status = mDRV2625.initERM();
    if (status == false) {
        LOG_ERROR("initERM() failed\n");
        goto exit;
    }

    if (getConfig(bemfGain, calComp, calBmf)) {

        // Apply config
        status = mDRV2625.applyCalibrationERM(
                (Hardware::DRV2625::BemfGain) bemfGain, calComp, calBmf);
        if (status == false) {
            LOG_ERROR("applyCalibrationERM() failed\n");
            goto exit;
        }
    } else {
        status = mDRV2625.calibrationERM((Hardware::DRV2625::BemfGain&) bemfGain, calComp, calBmf);
        if (status == false) {
            LOG_ERROR("calibrationERM() failed\n");
            goto exit;
        }
        // Save calibration data
        saveConfig(bemfGain, calComp, calBmf);
        LOG_DEBUG("Config:  bemfGain %u, calComp %u, calBmf %u\n", bemfGain, calComp, calBmf);
    }

    // Use Waveform Sequencer
    status = mDRV2625.setMode(Hardware::DRV2625::Mode::WAVEFORM_SEQUENCER);
    if (status == false) {
        LOG_ERROR("setMode() failed\n");
        goto exit;
    }

    status = mDRV2625.setLib(Hardware::DRV2625::MotorType::ERM);
    if (status == false) {
        LOG_ERROR("setLib() failed\n");
        goto exit;
    }

exit:
    mIsOn = status;

    if (status) {
        LOG_DEBUG("Enabled\n");
    } else {
        mpRst->set(false);   // shutdown
        LOG_ERROR("Failed to enable\n");
    }

    mMutex.release();
    return status;
}

bool Vibro::disable()
{
    if (!mIsInit) {
        return false;
    }

    if (mMutex.lock()) {
        mIsOn = false;
        mpRst->set(false);
        LOG_DEBUG("Disabled\n");
        mMutex.release();
    }

    return true;
}

bool Vibro::isEnabled()
{
    if (!mIsInit) {
        return false;
    }

    return mIsOn;
}

void Vibro::play(uint8_t effect)
{
    Note melody {};
    melody.effect = effect & 0x7F;
    play(&melody, 1);
}

void Vibro::play(const Note melody[], uint8_t len, uint8_t loop)
{
    if (!mIsInit || !mIsOn) {
        return;
    }

    if (len > skMaxNotes) {
        len = skMaxNotes;
    }

    if (!mMutex.lock(skMutexTimeout)) {
        return;
    }
    bool status = true;

    mDRV2625.stop();    // Stop previous melody

    for (uint8_t i = 0; i < len; i++) {
        if (melody[i].effect != Effect::NO_EFFECT) {
            status = mDRV2625.setSequencer(i, false,
                    (((uint8_t) melody[i].effect) & 0x7F),
                    (melody[i].loop & 0x03));
            if (status == false) {
                LOG_ERROR("setSequencer() failed\n");
                goto exit;
            }
        } else if (melody[i].pause >= 10) {
            status = mDRV2625.setSequencer(i, true, (melody[i].pause / 10), 0);
            if (status == false) {
                LOG_ERROR("setSequencer() failed\n");
                goto exit;
            }
        } else {
            status = mDRV2625.setSequencer(i, false, 0, 0); // stop
            if (status == false) {
                LOG_ERROR("setSequencer() failed\n");
                goto exit;
            }
            break;
        }
    }

    // Last one must be zero
    if (len < (Interface::IVibro::skMaxNotes - 1)) {
        status = mDRV2625.setSequencer(len, false, 0, 0); // stop
        if (status == false) {
            LOG_ERROR("setSequencer() failed\n");
            goto exit;
        }
    }

    status = mDRV2625.setMainLoop(loop & 0x07);
    if (status == false) {
        LOG_ERROR("setMainLoop() failed\n");
        goto exit;
    }

    if (status) {
        status = mDRV2625.go();
        if (status == false) {
            LOG_ERROR("go() failed\n");
            goto exit;
        }
    }


exit:
    if (status == false) {
        LOG_ERROR("Play failed\n");
        mDRV2625.stop();
    } else {
        LOG_DEBUG("Play\n");
    }

    mMutex.release();
}

bool Vibro::isPlaying()
{
    if (!mIsInit || !mIsOn) {
        return false;
    }
    return mDRV2625.getGoFlag();
}

void Vibro::stop()
{
    if (!mIsInit || !mIsOn) {
        return;
    }

    if (mMutex.lock(skMutexTimeout)) {
        mDRV2625.stop();
        LOG_DEBUG("Stop\n");
        mMutex.release();
    }
}

bool Vibro::getConfig(uint8_t &bemfGain, uint8_t &calComp, uint8_t &calBmf)
{
#if (VIBRO_USE_SETTINGS_IN_RTC == 1)
    VibroConfigBackupValue bkp;
    bkp.raw = Driver::Rtc::GetInstance().readBackupReg(skConfigBkpReg);

    bemfGain = bkp.bemfGain;
    calComp = bkp.calComp;
    calBmf = bkp.calBmf;

    return (bemfGain != 0 && calComp != 0 && calBmf != 0);
#else
    return false;
#endif
}

bool Vibro::saveConfig(uint8_t bemfGain, uint8_t calComp, uint8_t calBmf)
{
#if (VIBRO_USE_SETTINGS_IN_RTC == 1)
    VibroConfigBackupValue bkp;
    bkp.bemfGain = bemfGain;
    bkp.calComp = calComp;
    bkp.calBmf = calBmf;
    Driver::Rtc::GetInstance().writeBackupReg(skConfigBkpReg, bkp.raw);
    return true;
#else
    return false;
#endif
}
