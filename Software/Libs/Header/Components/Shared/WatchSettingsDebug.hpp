/**
 ******************************************************************************
 * @file    WatchSettingsDebug.hpp
 * @brief   Debug watch settings configuration structure.
 ******************************************************************************
 * This file contains the definition of the "WatchSettingsDebug" structure,
 * which represents debug-specific settings.
 ******************************************************************************
 */

#ifndef __WATCH_SETTINGS_DEBUG_HPP
#define __WATCH_SETTINGS_DEBUG_HPP

#include <cstdint>

/**
 * @brief Debug device settings configuration structure.
 */
struct WatchSettingsDebug {

    /// Settings version.
    static constexpr uint8_t kVersion = 1;


    uint32_t version = kVersion;    ///< Settings version


    // PixArt PAH8316LS
    struct {
        bool     manufLog = false;
    } ppgCfg;

    // Airoha AG3335
    struct {
        bool     nmeaLog  = false;
        bool     imuLog   = false;
        uint8_t  binLog   = 0;
        uint8_t  MinSNR   = 9;
        float    hdopTh   = 0.0f;
        uint32_t haacLim  = 0;
        int32_t  elevMask = 5;
    } gpsCfg;

    // Bosch BMI270
    struct {
        bool     enable           = false; // Use this config
        uint16_t minAngleFocus    = 1024;
        uint16_t minAngleNonfocus = 1774;
        uint16_t maxTiltLr        = 1024;
        uint16_t maxTiltLl        = 700;
        uint16_t maxTiltPd        = 179;
        uint16_t maxTiltPu        = 1925;
    } wristCfg;

    // Battery
    struct {
        bool     logEnable = false;
        uint32_t period    = 60;
    } battCfg;

    // BLE BlueNRG2
    struct {
        bool     useNUS = false; // Use Nordic UART Service (NUS) instead of Notification service
    } bleCfg;

    // Log config
    struct {
        bool     logToBLE  = false; // Redirect debug log to the NUS Tx characteristic. Works only if bleCfg.useNUS = true
        bool     logToFile = false; // Drain the system log to rolling files in 2:/Log/System/ (SystemLogger)
        bool     logInDam  = false; // Developer bypass: keep kernel log enabled while DAM is active (default off = kernel log suppressed in DAM)
    } logCfg;


    // Fusion sensor config
    struct {
        bool     logToCSV = false; // Log data from fusion to CSV file
        bool     logToBLE = false; // Stream data from fusion to BLE. Works only if bleCfg.useNUS = true
    } fusionCfg;

    /**
     * @brief Equality operator to compare two local settings configurations.
     * @param other The other WatchSettingsDebug object to compare.
     * @return 'true' if the configurations are equal, 'false' otherwise.
     *
     * Compares all local settings fields to determine equality.
     */
    bool operator==(const WatchSettingsDebug &other) const
    {
        return (ppgCfg.manufLog     == other.ppgCfg.manufLog &&
                gpsCfg.imuLog       == other.gpsCfg.imuLog &&
                gpsCfg.binLog       == other.gpsCfg.binLog &&
                gpsCfg.MinSNR       == other.gpsCfg.MinSNR &&
                gpsCfg.hdopTh       == other.gpsCfg.hdopTh &&
                gpsCfg.haacLim      == other.gpsCfg.haacLim &&
                gpsCfg.elevMask     == other.gpsCfg.elevMask &&

                wristCfg.minAngleFocus      == other.wristCfg.minAngleFocus &&
                wristCfg.minAngleNonfocus   == other.wristCfg.minAngleNonfocus &&
                wristCfg.maxTiltLr          == other.wristCfg.maxTiltLr &&
                wristCfg.maxTiltLl          == other.wristCfg.maxTiltLl &&
                wristCfg.maxTiltPd          == other.wristCfg.maxTiltPd &&
                wristCfg.maxTiltPu          == other.wristCfg.maxTiltPu &&

                battCfg.logEnable   == other.battCfg.logEnable &&
                battCfg.period      == other.battCfg.period &&

                bleCfg.useNUS == other.bleCfg.useNUS &&

                logCfg.logToBLE == other.logCfg.logToBLE &&
                logCfg.logToFile == other.logCfg.logToFile &&
                logCfg.logInDam == other.logCfg.logInDam &&

                fusionCfg.logToCSV == other.fusionCfg.logToCSV &&
                fusionCfg.logToBLE == other.fusionCfg.logToBLE
        );
    }

    /**
     * @brief Inequality operator to compare two local settings configurations.
     * @param other The other WatchSettingsDebug object to compare.
     * @return 'true' if the configurations are not equal, 'false' otherwise.
     */
    bool operator!=(const WatchSettingsDebug &other) const
    {
        return !(*this == other);
    }
};

#endif // __WATCH_SETTINGS_DEBUG_HPP
