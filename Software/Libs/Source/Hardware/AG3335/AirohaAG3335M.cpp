/**
 ******************************************************************************
 * @file    AirohaAG3335M.cpp
 * @brief   Driver for Airoha AG3335M.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "AG3335/AirohaAG3335M.hpp"

#define LOG_MODULE_PRX      "Airoha.AG3335M"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>

namespace Airoha
{

AG3335M::AG3335M()
{

}

AG3335M::AG3335M(Interface::IUart &uart, Interface::IGpo &pwr) :
        AirohaBase(uart, pwr)
{
}

AG3335M::~AG3335M()
{
}



bool AG3335M::softPowerOn(uint32_t timeout)
{
    const uint32_t ackTimeout = 1000;
    return cmdGeneric("002",
            std::max(ackTimeout, timeout),
            timeout > 0);
}

bool AG3335M::softPowerOff(uint32_t timeout)
{
    const uint32_t ackTimeout = 1000;
    return cmdGeneric("003",
            std::max(ackTimeout, timeout),
            timeout > 0);
}

bool AG3335M::start(StartType startType, uint32_t timeout)
{
    const uint32_t ackTimeout = 5000;
    switch (startType) {
        case StartType::HOT:
            return cmdGeneric("004",
                    std::max(ackTimeout, timeout),
                    timeout > 0);
        case StartType::WARM:
            return cmdGeneric("005",
                    std::max(ackTimeout, timeout),
                    timeout > 0);
        case StartType::COLD:
            return cmdGeneric("006",
                    std::max(ackTimeout, timeout),
                    timeout > 0);

        default:
            assert(false);
            return false;
    }
}

bool AG3335M::setDebugLog(DebugLog log, uint32_t timeout)
{
    bool status = false;
    const uint32_t ackTimeout = 500;

    Command::SetDebugLogOutput *cmd = nullptr;
    cmd = mContextPool.acquire<Command::SetDebugLogOutput>();
    if (!cmd) {
        return false;
    }
    cmd->setDebugLog(log);
    cmd->setTimeout(std::max(ackTimeout, timeout));

    if (timeout > 0) {
        status = sendCommandSync(cmd);
    } else {
        status = sendCommandAsync(cmd);
    }

    cmd->release();

    return status;
}

bool AG3335M::getDebugLog(DebugLog &log)
{
    bool status = false;
    const uint32_t rspTimeout = 500;

    Command::GetDebugLogOutput *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GetDebugLogOutput>();
    if (!cmd) {
        return false;
    }

    cmd->setHasResponse();
    cmd->setTimeout(rspTimeout);

    status = sendCommandSync(cmd);
    if (status) {
        log = cmd->getDebugLog();
    }

    cmd->release();

    return status;
}

bool AG3335M::setHACCLimit(uint32_t lim, uint32_t timeout)
{
    // Horizontal accuracy limit for a fix: 30-200 m, or 0 for none
    if (lim != 0 && (lim < 30 || lim > 200)) {
        return false;
    }
    const uint32_t ackTimeout = 500;

    return cmdGenericSetUINT("064", lim,
            std::max(ackTimeout, timeout),
            timeout > 0);
}

bool AG3335M::getHACCLimit(uint32_t &lim)
{
    const uint32_t rspTimeout = 500;
    return cmdGenericGetUINT("065", lim, rspTimeout);
}

bool AG3335M::setMinSNR(uint32_t snr, uint32_t timeout)
{
    // Minimum SNR of the satellites used: 9-37
    if (snr < 9 || snr > 37) {
        return false;
    }
    const uint32_t ackTimeout = 500;
    return cmdGenericSetUINT("058", snr,
            std::max(ackTimeout, timeout),
            timeout > 0);
}

bool AG3335M::getMinSNR(uint32_t &snr)
{
    const uint32_t rspTimeout = 500;
    return cmdGenericGetUINT("059", snr, rspTimeout);
}

bool AG3335M::setHDOPThreshold(float th, uint32_t timeout)
{
    const uint32_t ackTimeout = 500;
    return cmdGenericSetFLOAT("068", th, 1,
            std::max(ackTimeout, timeout),
            timeout > 0);
}

bool AG3335M::getHDOPThreshold(float &th)
{
    const uint32_t rspTimeout = 500;
    return cmdGenericGetFLOAT("069", th, rspTimeout);
}

bool AG3335M::setElevMask(int32_t mask, uint32_t timeout)
{
    // Elevation mask in degrees: -90 to 90
    if (mask < -90 || mask > 90) {
        return false;
    }
    const uint32_t ackTimeout = 500;
    return cmdGenericSetINT("072", mask,
            std::max(ackTimeout, timeout),
            timeout > 0);
}

bool AG3335M::getElevMask(int32_t &mask)
{
    const uint32_t rspTimeout = 500;
    return cmdGenericGetINT("073", mask, rspTimeout);
}

bool AG3335M::enableOdometer(bool state, uint32_t timeout)
{
    const uint32_t ackTimeout = 500;
    return cmdGenericSetBOOL("164", state,
            std::max(ackTimeout, timeout),
            timeout > 0);
}

bool AG3335M::getOdometerStatus(bool &state, float &distance)
{
    bool status = false;
    const uint32_t rspTimeout = 500;

    Command::GetOdometerStatus *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GetOdometerStatus>();
    if (!cmd) {
        return false;
    }

    cmd->setHasResponse();
    cmd->setTimeout(rspTimeout);

    status = sendCommandSync(cmd);
    if (status) {
        state = cmd->isEnabled();
        distance = cmd->getDistanse();
    }

    cmd->release();

    return status;
}

bool AG3335M::enableDualBandMode(bool state, uint32_t timeout)
{
    const uint32_t ackTimeout = 500;
    bool status = cmdGenericSetBOOL("104", state,
            std::max(ackTimeout, timeout),
            timeout > 0);

    if (status) {
        mUseL1Only = !state;
    }

    return status;
}

bool AG3335M::getDualBandMode(bool &state)
{
    const uint32_t rspTimeout = 500;
    return cmdGenericGetBOOL("105", state, rspTimeout);
}

bool AG3335M::lockSleep(uint32_t timeout)
{
    const uint32_t ackTimeout = 500;
    return cmdGenericSetUINT("382", 1,
            std::max(ackTimeout, timeout),
            timeout > 0);
}

bool AG3335M::unlockSleep(uint32_t timeout)
{
    const uint32_t ackTimeout = 500;
    return cmdGenericSetUINT("382", 0,
            std::max(ackTimeout, timeout),
            timeout > 0);
}

} // namespace Airoha
