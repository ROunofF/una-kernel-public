/**
 ******************************************************************************
 * @file    DRV2625.hpp
 * @brief    Haptic Driver with Built-In Library.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __HARDWARE_DRV2625_HPP
#define __HARDWARE_DRV2625_HPP

#include <cstdint>
#include <cstdbool>

#include "II2c.hpp"

namespace Hardware
{

/**
 * @brief    Haptic Driver.
 */
class DRV2625
{

public:

    // I2C addresses
    static const uint8_t skI2cAddr = 0x5A;
    static const uint8_t skI2cAddrBroadcast = 0x58;

    // Device identification information
    static const uint8_t skChipID = 1;      // DRV2625
    static const uint8_t skRevision = 3;    // Rev 2 and 3 are both commercially released.

    /**
     * @brief   Motor type
     */
    enum class MotorType : uint8_t {
        ERM = 0,
        LRA,
    };

    /**
     * @brief   Function of the TRIG/INTZ pin.
     * @brief   Used in Register 0x07
     */
    enum class TriggerPinFunction : uint8_t {
        EXT_PULSE = 0,
        EXT_LEVEL,
        INTERRUPT
    };

    /**
     * @brief   Operation mode.
     * @brief   Used in Register 0x07
     */
    enum class Mode : uint8_t {
        REAL_TIME = 0,
        WAVEFORM_SEQUENCER,
        DIAGNOSTICS_ROUTINE,
        AUTO_LEVEL_CALIB
    };

    /**
     * @brief   Internal memory playback interval.
     * @brief   Used in Register 0x0D
     */
    enum class PlaybackInterval : uint8_t {
        INT_5MS = 0,
        INT_1MS
    };

    /**
     * @brief   This parameter allows for proportionally scaling down
     *          (attenuating) the effects stored in the internal library to
     *          simplify the customization of haptics. This parameter is
     *          ignored in RTP mode.
     * @brief   Used in Register 0x0D
     */
    enum class DigMemGain : uint8_t {
        GAIN_100 = 0,
        GAIN_75,
        GAIN_50,
        GAIN_25
    };

    /**
     * @brief   Analog gain of the back-EMF amplifier..
     * @brief   Used in Register 0x23
     */
    enum class BemfGain : uint8_t {
        GAIN_5X = 0,
        GAIN_10X,
        GAIN_20X,
        GAIN_30X
    };

    /**
     * @brief   Minimum frequency supported by the device.
     * @brief   Used in Register 0x27
     */
    enum class MinFreq : uint8_t {
        FRQ_125HZ = 0,
        FRQ_145HZ
    };

    /**
     * @brief   Initial guess for LRA drive time in LRA mode.
     *          Sample rate for the back-EMF detection in ERM mode.
     * @brief   Used in Register 0x27
     */
    enum class DriveTimeUs : uint8_t {
        LRA500ERM1000 = 0,
        LRA600ERM1200,
        LRA700ERM1400,
        LRA800ERM1600,
        LRA900ERM1800,
        LRA1000ERM2000,
        LRA1100ERM2200,
        LRA1200ERM2400,
        LRA1300ERM2600,
        LRA1400ERM2800,
        LRA1500ERM3000,
        LRA1600ERM3200,
        LRA1700ERM3400,
        LRA1800ERM3600,
        LRA1900ERM3800,
        LRA2000ERM4000,
        LRA2100ERM4200,
        LRA2200ERM4400,
        LRA2300ERM4600,
        LRA2400ERM4800,
        LRA2500ERM5000,
        LRA2600ERM5200,
        LRA2700ERM5400,
        LRA2800ERM5600,
        LRA2900ERM5800,
        LRA3000ERM6000,
        LRA3100ERM6200,
        LRA3200ERM6400,
        LRA3300ERM6600,
        LRA3400ERM6800,
        LRA3500ERM7000,
        LRA3600ERM7200
    };

    /**
     * @brief   Time waiting for BEMF to settle. Before ADC sampling.
     * @brief   Used in Register 0x28
     */
    enum class BlankingOrIdissTimeUs : uint8_t {
        LRA15ERM45 = 0,
        LRA25ERM75,
        LRA50ERM150,
        LRA75ERM225,
        LRA90,
        LRA105,
        LRA120,
        LRA135,
        LRA150,
        LRA165,
        LRA180,
        LRA195,
        LRA210,
        LRA235,
        LRA260,
        LRA285,
    };

    /**
     * @brief   Time to wait before/after zero-crossing before adc samples
     *          BEMF amplitude.
     * @brief   Used in Register 0x29
     */
    enum class SampleTime : uint8_t {
        T_150US = 0,
        T_200US,
        T_250US,
        T_300US
    };

    /**
     * @brief   Zero crossing detection time.
     * @brief   Used in Register 0x29
     */
    enum class ZcDetTime : uint8_t {
        T_100US = 0,
        T_200US,
        T_300US,
        T_390US
    };

    /**
     * @brief   Duration of autocal routine.
     * @brief   Used in Register 0x2A
     */
    enum class AutoCalTime : uint8_t {
        T_250MS = 0,
        T_500MS,
        T_1000MS,
        TRIGGER_CONTROLLED
    };

    /**
     * @brief   Shape to use for driving the LRA when in open loop mode.
     * @brief   Used in Register 0x2C
     */
    enum class WaveShape : uint8_t {
        SQUARE = 0,
        SINE
    };

    /**
     * @brief   Bitmask of register 0x00.
     */
    union Reg00 {
        struct {
            uint8_t REV :4;
            uint8_t CHIPID :4;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x01.
     */
    union Reg01 {
        struct {
            uint8_t OC_DETECT :1;
            uint8_t OVER_TEMP :1;
            uint8_t UVLO :1;
            uint8_t PROCESS_DONE :1;
            uint8_t :3;
            uint8_t DIAG_RESULT :1;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x02.
     */
    union Reg02 {
        struct {
            uint8_t TOGGLE_OC_DETECT :1;
            uint8_t TOGGLE_OVER_TEMP :1;
            uint8_t TOGGLE_UVLO :1;
            uint8_t TOGGLE_PROCESS_DONE :1;
            uint8_t :4;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x07.
     */
    union Reg07 {
        struct {
            uint8_t MODE :2;
            uint8_t TRIG_PIN_FUNC :2;
            uint8_t LINEREG_COMP_SEL :2;
            uint8_t LRA_PERIOD_AVG_DIS :1;
            uint8_t I2C_BCAST_EN :1;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x08.
     */
    union Reg08 {
        struct {
            uint8_t :2;
            uint8_t INPUT_SLOPE_CHECK :1;
            uint8_t AUTO_BRK_INTO_STBY :1;
            uint8_t AUTO_BRK_OL :1;
            uint8_t HYBRID_LOOP :1;
            uint8_t CONTROL_LOOP :1;
            uint8_t LRA_ERM :1;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x09.
     */
    union Reg09 {
        struct {
            uint8_t UVLO_THRES :3;
            uint8_t :3;
            uint8_t BAT_LIFE_EXT_LVL_EN :2;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x0C.
     */
    union Reg0C {
        struct {
            uint8_t GO :1;
            uint8_t :7;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x0D.
     */
    union Reg0D {
        struct {
            uint8_t DIG_MEM_GAIN :2;
            uint8_t :3;
            uint8_t PLAYBACK_INTERVAL :1;
            uint8_t LIB_SEL :1;
            uint8_t LIB_EN :1;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x19.
     */
    union Reg19 {
        struct {
            uint8_t WAV_SEQ_MAIN_LOOP :3;
            uint8_t :5;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x23.
     */
    union Reg23 {
        struct {
            uint8_t BEMF_GAIN :2;
            uint8_t LOOP_GAIN :2;
            uint8_t FB_BRAKE_FACTOR :3;
            uint8_t NG_THRESH :1;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x27.
     */
    union Reg27 {
        struct {
            uint8_t DRIVE_TIME :5;
            uint8_t :1;
            uint8_t LRA_RESYNC_FORMAT :1;
            uint8_t LRA_MIN_FREQ_SEL :1;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x28.
     */
    union Reg28 {
        struct {
            uint8_t IDISS_TIME :4;
            uint8_t BLANKING_TIME :4;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x29.
     */
    union Reg29 {
        struct {
            uint8_t ZC_DET_TIME :2;
            uint8_t SAMPLE_TIME :2;
            uint8_t OD_CLAMP_TIME :2;
            uint8_t :2;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x2A.
     */
    union Reg2A {
        struct {
            uint8_t AUTO_CAL_TIME :2;
            uint8_t :6;
        };
        uint8_t reg;
    };

    /**
     * @brief   Bitmask of register 0x2C.
     */
    union Reg2C {
        struct {
            uint8_t LRA_WAVE_SHAPE :1;
            uint8_t :4;
            uint8_t AUTO_OL_CNT :2;
            uint8_t LRA_AUTO_OPEN_LOOP :1;
        };
        uint8_t reg;
    };

    /**
     * @brief   Waveform Library Effects List.
     */
    enum class WaveformEffect : uint8_t {
        NO_EFFECT = 0,  // silent
        STRONG_CLICK_100 = 1,
        STRONG_CLICK_60 = 2,
        STRONG_CLICK_30 = 3,
        SHARP_CLICK_100 = 4,
        SHARP_CLICK_60 = 5,
        SHARP_CLICK_30 = 6,
        SOFT_BUMP_100 = 7,
        SOFT_BUMP_60 = 8,
        SOFT_BUMP_30 = 9,
        DOUBLE_CLICK_100 = 10,
        DOUBLE_CLICK_60 = 11,
        TRIPLE_CLICK_100 = 12,
        SOFT_FUZZ_60 = 13,
        STRONG_BUZZ_100 = 14,
        ALERT_750MS_100 = 15,
        ALERT_1000MS_100 = 16,
        STRONG_CLICK_1_100 = 17,
        STRONG_CLICK_2_80 = 18,
        STRONG_CLICK_3_60 = 19,
        STRONG_CLICK_4_30 = 20,
        MEDIUM_CLICK_1_100 = 21,
        MEDIUM_CLICK_2_80 = 22,
        MEDIUM_CLICK_3_60 = 23,
        SHARP_TICK_1_100 = 24,
        SHARP_TICK_2_80 = 25,
        SHARP_TICK_3_60 = 26,
        SHORT_DOUBLE_CLICK_STRONG_1_100 = 27,
        SHORT_DOUBLE_CLICK_STRONG_2_80 = 28,
        SHORT_DOUBLE_CLICK_STRONG_3_60 = 29,
        SHORT_DOUBLE_CLICK_STRONG_4_30 = 30,
        SHORT_DOUBLE_CLICK_MEDIUM_1_100 = 31,
        SHORT_DOUBLE_CLICK_MEDIUM_2_80 = 32,
        SHORT_DOUBLE_CLICK_MEDIUM_3_60 = 33,
        SHORT_DOUBLE_SHARP_TICK_1_100 = 34,
        SHORT_DOUBLE_SHARP_TICK_2_80 = 35,
        SHORT_DOUBLE_SHARP_TICK_3_60 = 36,
        LONG_DOUBLE_SHARP_CLICK_STRONG_1_100 = 37,
        LONG_DOUBLE_SHARP_CLICK_STRONG_2_80 = 38,
        LONG_DOUBLE_SHARP_CLICK_STRONG_3_60 = 39,
        LONG_DOUBLE_SHARP_CLICK_STRONG_4_30 = 40,
        LONG_DOUBLE_SHARP_CLICK_MEDIUM_1_100 = 41,
        LONG_DOUBLE_SHARP_CLICK_MEDIUM_2_80 = 42,
        LONG_DOUBLE_SHARP_CLICK_MEDIUM_3_60 = 43,
        LONG_DOUBLE_SHARP_TICK_1_100 = 44,
        LONG_DOUBLE_SHARP_TICK_2_80 = 45,
        LONG_DOUBLE_SHARP_TICK_3_60 = 46,
        BUZZ_1_100 = 47,
        BUZZ_2_80 = 48,
        BUZZ_3_60 = 49,
        BUZZ_4_40 = 50,
        BUZZ_5_20 = 51,
        PULSING_STRONG_1_100 = 52,
        PULSING_STRONG_2_60 = 53,
        PULSING_MEDIUM_1_100 = 54,
        PULSING_MEDIUM_2_60 = 55,
        PULSING_SHARP_1_100 = 56,
        PULSING_SHARP_2_60 = 57,
        TRANSITION_CLICK_1_100 = 58,
        TRANSITION_CLICK_2_80 = 59,
        TRANSITION_CLICK_3_60 = 60,
        TRANSITION_CLICK_4_40 = 61,
        TRANSITION_CLICK_5_20 = 62,
        TRANSITION_CLICK_6_10 = 63,
        TRANSITION_HUM_1_100 = 64,
        TRANSITION_HUM_2_80 = 65,
        TRANSITION_HUM_3_60 = 66,
        TRANSITION_HUM_4_40 = 67,
        TRANSITION_HUM_5_20 = 68,
        TRANSITION_HUM_6_10 = 69,
        TRANSITION_RAMP_DOWN_LONG_SMOOTH_1_100_TO_0 = 70,
        TRANSITION_RAMP_DOWN_LONG_SMOOTH_2_100_TO_0 = 71,
        TRANSITION_RAMP_DOWN_MEDIUM_SMOOTH_1_100_TO_0 = 72,
        TRANSITION_RAMP_DOWN_MEDIUM_SMOOTH_2_100_TO_0 = 73,
        TRANSITION_RAMP_DOWN_SHORT_SMOOTH_1_100_TO_0 = 74,
        TRANSITION_RAMP_DOWN_SHORT_SMOOTH_2_100_TO_0 = 75,
        TRANSITION_RAMP_DOWN_LONG_SHARP_1_100_TO_0 = 76,
        TRANSITION_RAMP_DOWN_LONG_SHARP_2_100_TO_0 = 77,
        TRANSITION_RAMP_DOWN_MEDIUM_SHARP_1_100_TO_0 = 78,
        TRANSITION_RAMP_DOWN_MEDIUM_SHARP_2_100_TO_0 = 79,
        TRANSITION_RAMP_DOWN_SHORT_SHARP_1_100_TO_0 = 80,
        TRANSITION_RAMP_DOWN_SHORT_SHARP_2_100_TO_0 = 81,
        TRANSITION_RAMP_UP_LONG_SMOOTH_1_0_TO_100 = 82,
        TRANSITION_RAMP_UP_LONG_SMOOTH_2_0_TO_100 = 83,
        TRANSITION_RAMP_UP_MEDIUM_SMOOTH_1_0_TO_100 = 84,
        TRANSITION_RAMP_UP_MEDIUM_SMOOTH_2_0_TO_100 = 85,
        TRANSITION_RAMP_UP_SHORT_SMOOTH_1_0_TO_100 = 86,
        TRANSITION_RAMP_UP_SHORT_SMOOTH_2_0_TO_100 = 87,
        TRANSITION_RAMP_UP_LONG_SHARP_1_0_TO_100 = 88,
        TRANSITION_RAMP_UP_LONG_SHARP_2_0_TO_100 = 89,
        TRANSITION_RAMP_UP_MEDIUM_SHARP_1_0_TO_100 = 90,
        TRANSITION_RAMP_UP_MEDIUM_SHARP_2_0_TO_100 = 91,
        TRANSITION_RAMP_UP_SHORT_SHARP_1_0_TO_100 = 92,
        TRANSITION_RAMP_UP_SHORT_SHARP_2_0_TO_100 = 93,
        TRANSITION_RAMP_DOWN_LONG_SMOOTH_1_50_TO_0 = 94,
        TRANSITION_RAMP_DOWN_LONG_SMOOTH_2_50_TO_0 = 95,
        TRANSITION_RAMP_DOWN_MEDIUM_SMOOTH_1_50_TO_0 = 96,
        TRANSITION_RAMP_DOWN_MEDIUM_SMOOTH_2_50_TO_0 = 97,
        TRANSITION_RAMP_DOWN_SHORT_SMOOTH_1_50_TO_0 = 98,
        TRANSITION_RAMP_DOWN_SHORT_SMOOTH_2_50_TO_0 = 99,
        TRANSITION_RAMP_DOWN_LONG_SHARP_1_50_TO_0 = 100,
        TRANSITION_RAMP_DOWN_LONG_SHARP_2_50_TO_0 = 101,
        TRANSITION_RAMP_DOWN_MEDIUM_SHARP_1_50_TO_0 = 102,
        TRANSITION_RAMP_DOWN_MEDIUM_SHARP_2_50_TO_0 = 103,
        TRANSITION_RAMP_DOWN_SHORT_SHARP_1_50_TO_0 = 104,
        TRANSITION_RAMP_DOWN_SHORT_SHARP_2_50_TO_0 = 105,
        TRANSITION_RAMP_UP_LONG_SMOOTH_1_0_TO_50 = 106,
        TRANSITION_RAMP_UP_LONG_SMOOTH_2_0_TO_50 = 107,
        TRANSITION_RAMP_UP_MEDIUM_SMOOTH_1_0_TO_50 = 108,
        TRANSITION_RAMP_UP_MEDIUM_SMOOTH_2_0_TO_50 = 109,
        TRANSITION_RAMP_UP_SHORT_SMOOTH_1_0_TO_50 = 110,
        TRANSITION_RAMP_UP_SHORT_SMOOTH_2_0_TO_50 = 111,
        TRANSITION_RAMP_UP_LONG_SHARP_1_0_TO_50 = 112,
        TRANSITION_RAMP_UP_LONG_SHARP_2_0_TO_50 = 113,
        TRANSITION_RAMP_UP_MEDIUM_SHARP_1_0_TO_50 = 114,
        TRANSITION_RAMP_UP_MEDIUM_SHARP_2_0_TO_50 = 115,
        TRANSITION_RAMP_UP_SHORT_SHARP_1_0_TO_50 = 116,
        TRANSITION_RAMP_UP_SHORT_SHARP_2_0_TO_50 = 117,
        LONG_BUZZ_PROGRAMMATIC_STOPPING_100 = 118,
        SMOOTH_HUM_1_NO_KICK_BRAKE_PULSE_50 = 119,
        SMOOTH_HUM_2_NO_KICK_BRAKE_PULSE_40 = 120,
        SMOOTH_HUM_3_NO_KICK_BRAKE_PULSE_30 = 121,
        SMOOTH_HUM_4_NO_KICK_BRAKE_PULSE_20 = 122,
        SMOOTH_HUM_5_NO_KICK_BRAKE_PULSE_10 = 123,
    };

    /**
     * @brief   Constructor.
     */
    DRV2625() = default;

    /**
     * @brief   Constructor.
     * @param   pI2c: pointer to I2C.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     */
    DRV2625(Interface::II2c *pI2c, uint8_t devAddr = skI2cAddr);

    /**
     * @brief   Destructor.
     */
    virtual ~DRV2625() = default;

    /**
     * @brief   Configure chip with I2C driver.
     * @param   pI2c: pointer to an I2C.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool config(Interface::II2c *pI2c, uint8_t devAddr = skI2cAddr);

    /**
     * @brief   Checks if chip is ready for communication.
     * @retval  Execution status. 'true' - device ready, 'false' - device
     *          not responded.
     */
    bool isReady();

    /**
     * @brief   Read register.
     * @param   reg: register to read.
     * @param   v: reference to save register value.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readReg(uint8_t reg, uint8_t &v);

    /**
     * @brief   Write register.
     * @param   reg: register to write.
     * @param   v: new register value.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool writeReg(uint8_t reg, uint8_t value);

    uint8_t getDeviceId();
    uint8_t getDeviceRevision();

    bool diagnostic();
    bool initERM();
    bool calibrationERM(BemfGain &bemfGain, uint8_t &calComp, uint8_t &calBmf);
    bool applyCalibrationERM(BemfGain bemfGain, uint8_t calComp,
            uint8_t calBmf);

    bool enableI2CBroadcast(bool en);
    bool enableLraPeriodAverage(bool en);
    bool enableControlLoop(bool en);
    bool enableAutoBreak(bool en, bool openLoop = false);
    bool enableHybridLoop(bool en);
    bool setTriggerPinFunction(TriggerPinFunction function);
    bool setMode(Mode mode);
    bool setMotorType(MotorType type);
    bool setLib(MotorType type);
    bool setPlaybackInterval(PlaybackInterval interval);
    bool setDigMemGain(DigMemGain gain);
    bool setRealTimeAmplitude(uint8_t amplitude);
    bool setOverdriveOpenLoop(uint8_t value);
    bool setRatedMotorVoltage(uint8_t value);
    bool setODClamp(uint8_t value);
    bool setAutoCalTime(AutoCalTime time);
    bool setBlankingTime(BlankingOrIdissTimeUs time);
    bool setIdissTime(BlankingOrIdissTimeUs time);
    bool setFbBrakeFactor(uint8_t value);
    bool setLoopGain(uint8_t value);
    bool setLraMinFrequency(MinFreq freq);
    bool setDriveTime(DriveTimeUs value);
    bool setLraWaveShape(WaveShape wave);
    bool setSampleTime(SampleTime time);
    bool setZcDetTime(ZcDetTime time);
    bool getDiagResFlag();
    bool getProcessDoneFlag();
    bool getUVLOFlag();
    bool getOverTempFlag();
    bool getOCDetectFlag();
    uint8_t getStatusFlags();
    uint8_t getDiagZResult();
    bool getGoFlag();
    uint8_t getVoltageCompensation();
    bool setVoltageCompensation(uint8_t value);
    uint8_t getFeedbackValue();
    bool setFeedbackValue(uint8_t value);
    BemfGain getFeedbackGain();
    bool setFeedbackGain(BemfGain gain);
    uint16_t getLraPeriod();

    bool setSequencer(uint8_t idx, bool pause, uint8_t value, uint8_t loop);
    bool setMainLoop(uint8_t loop);
    bool go();
    bool stop();

private:

    // Indicates that the chip is configured
    bool mIsConfigured { };

    // Hardware configuration
    uint8_t mAddr { };
    Interface::II2c *mpI2c { };

    // Number of a retries for I2C transactions
    static const uint32_t skI2cRetries = 4;

    /**
     * @brief   Set bits in register.
     * @param   reg: register.
     * @param   mask: bits to set.
     */
    void setBit(uint8_t &reg, uint8_t mask);

    /**
     * @brief   Clear bits in register.
     * @param   reg: register.
     * @param   mask: bits to clear.
     */
    void clearBit(uint8_t &reg, uint8_t mask);

    /**
     * @brief   Return system tick in ms
     * @retval  Current HAL time base time stamp
     */
    uint32_t getTick();

    /**
     * @brief   Delay.
     * @param   ms: time in milliseconds.
     */
    void delay(uint32_t ms);

    /**
     * @brief   Wait operation done.
     * @param   ms: time in milliseconds.
     */
    bool waitDone(uint32_t timeout);

};

} /* namespace Hardware */

#endif /* __HARDWARE_DRV2625_HPP */
