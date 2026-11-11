/**
 ******************************************************************************
 * @file    AirohaAG3335M.hpp
 * @brief   Driver for Airoha AG3335M.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "AG3335/AirohaBase.hpp"

#pragma once

namespace Airoha
{

class AG3335M : public AirohaBase {

public:
    AG3335M();
    AG3335M(Interface::IUart &uart, Interface::IGpo &pwr);
    virtual ~AG3335M();


    bool softPowerOn(uint32_t timeout = 0);
    bool softPowerOff(uint32_t timeout = 0);

    bool start(StartType startType, uint32_t timeout = 0);

    bool setDebugLog(DebugLog log, uint32_t timeout = 0);
    bool getDebugLog(DebugLog &log);

    bool setHACCLimit(uint32_t lim, uint32_t timeout = 0);
    bool getHACCLimit(uint32_t &lim);

    bool setMinSNR(uint32_t snr, uint32_t timeout = 0);
    bool getMinSNR(uint32_t &snr);

    bool setHDOPThreshold(float th, uint32_t timeout = 0);
    bool getHDOPThreshold(float &th);

    bool setElevMask(int32_t mask, uint32_t timeout = 0);
    bool getElevMask(int32_t &mask);

    bool enableOdometer(bool state, uint32_t timeout = 0);
    bool getOdometerStatus(bool &state, float &distance);

    bool enableDualBandMode(bool state, uint32_t timeout = 0);
    bool getDualBandMode(bool &state);

    bool lockSleep(uint32_t timeout = 0);
    bool unlockSleep(uint32_t timeout = 0);

private:



};

} // namespace Airoha
