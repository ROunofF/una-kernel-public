/**
 ******************************************************************************
 * @file    PeripheralLinkClients.cpp
 * @brief   Serialised GATT-client discovery coordinator for a single BLE link.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.GattClients"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "PeripheralLinkClients.hpp"

#include <cstring>

namespace Ble
{

PeripheralLinkClients& blePeripheralClients()
{
    static PeripheralLinkClients sManager;
    return sManager;
}

void PeripheralLinkClients::registerClient(IGattClient* client)
{
    if (client == nullptr || mClientCount >= kMaxClients) {
        return;
    }
    mClients[mClientCount++] = client;
}

void PeripheralLinkClients::start(uint16_t connHandle)
{
    if (mpOps == nullptr || mpSched == nullptr || mState != State::IDLE ||
            mClientCount == 0) {
        return;
    }

    mConnHandle = connHandle;
    mCurrent = 0;
    mInDescDiscovery = false;
    for (uint8_t i = 0; i < mClientCount; i++) {
        mResolved[i] = ResolvedService {};
    }

    LOG_INFO("Start: conn=0x%04X, %u client(s)\n", connHandle, mClientCount);
    beginClient(kSettleMs);
}

void PeripheralLinkClients::reset()
{
    // Cancel any pending discovery tick so it cannot fire against the torn-down
    // link after this returns.
    if (mpSched != nullptr) {
        mpSched->cancel();
    }

    mState = State::IDLE;
    mConnHandle = 0;
    mCurrent = 0;
    mDiscRetry = 0;
    mQuietWaits = 0;
    mInDescDiscovery = false;
    mStartHandle = 0;
    mEndHandle = 0;

    for (uint8_t i = 0; i < mClientCount; i++) {
        mResolved[i] = ResolvedService {};
        mClients[i]->onLinkDown();
    }
}

void PeripheralLinkClients::beginClient(uint32_t delayMs)
{
    mResolved[mCurrent] = ResolvedService {};
    mDiscRetry = 0;
    mQuietWaits = 0;
    mState = State::DELAYED_SRV_DISCOVERY;
    LOG_DEBUG("Client %u: begin discovery (in %lu ms)\n", mCurrent,
            (unsigned long) delayMs);
    mpSched->arm(delayMs);
}

void PeripheralLinkClients::advance()
{
    mCurrent++;
    if (mCurrent >= mClientCount) {
        mState = State::DONE;
        LOG_INFO("All clients resolved\n");
        for (uint8_t i = 0; i < mClientCount; i++) {
            mClients[i]->onLinkReady();
        }
        return;
    }
    beginClient(kStageMs);
}

void PeripheralLinkClients::clientReady()
{
    if (mState != State::AWAIT_CLIENT_READY) {
        return;
    }
    LOG_DEBUG("Client %u: ready\n", mCurrent);
    advance();
}

void PeripheralLinkClients::onTick()
{
    // Watchdog: a stage that found its target is waiting for the controller's
    // completion event (ATT 0x0A or procedure-complete). Heavy link traffic can
    // delay or drop that event; force progress so discovery never hangs.
    if (mState == State::SRV_DISCOVERED || mState == State::CHR_DISCOVERED) {
        LOG_WARNING("Client %u: stage-completion event missing, advancing\n",
                mCurrent);
        completeFoundStage();
        return;
    }

    // Descriptor discovery was issued but its procedure-complete never arrived:
    // resolve with the fallback CCCD positions rather than waiting forever.
    if (mInDescDiscovery) {
        LOG_WARNING("Client %u: descriptor-stage completion missing, resolving\n",
                mCurrent);
        resolveCurrent();
        return;
    }

    // A resolved client never reported it is ready: its setup stalled. Skip it
    // so the link is not blocked for the rest of the connection.
    if (mState == State::AWAIT_CLIENT_READY) {
        LOG_WARNING("Client %u: setup timed out, skipping\n", mCurrent);
        advance();
        return;
    }

    // Issue discovery only when the link is calm. A companion app can start its
    // own burst of transfers just as discovery begins; running into that storm
    // gets the commands rejected. "No traffic yet" reads as quiet, so the usual
    // case (link up before the app is busy) proceeds immediately.
    if (mState == State::DELAYED_SRV_DISCOVERY ||
            mState == State::DELAYED_CHR_DISCOVERY ||
            mState == State::DELAYED_DESC_DISCOVERY) {
        if (!transferQuiet(kQuietMs)) {
            if (++mQuietWaits <= kMaxQuietWaits) {
                mpSched->arm(kQuietPollMs);
                return;
            }
            // Waited long enough; proceed anyway (retries absorb a blip).
        }
        mQuietWaits = 0;
    }

    switch (mState) {
        case State::DELAYED_SRV_DISCOVERY:
            serviceDiscovery();
            break;
        case State::DELAYED_CHR_DISCOVERY:
            characteristicsDiscovery();
            break;
        case State::DELAYED_DESC_DISCOVERY:
            descriptorDiscovery();
            break;
        default:
            break;
    }
}

void PeripheralLinkClients::serviceDiscovery()
{
    LOG_DEBUG("Client %u: discovering service\n", mCurrent);
    if (mpOps->discoverPrimaryServiceByUuid(mConnHandle,
            mClients[mCurrent]->serviceUuid())) {
        // Waiting for onServiceGroupFound() / onGattError(0x0A).
        return;
    }

    if (++mDiscRetry <= kMaxDiscRetry) {
        LOG_DEBUG("Client %u: service discovery retry %u/%u\n", mCurrent,
                mDiscRetry, kMaxDiscRetry);
        mpSched->arm(kDiscRetryMs);
        return;
    }

    LOG_WARNING("Client %u: service discovery failed\n", mCurrent);
    mDiscRetry = 0;
    mClients[mCurrent]->onServiceUnavailable();
    advance();
}

void PeripheralLinkClients::onServiceGroupFound(uint16_t foundHandle,
        uint16_t endHandle)
{
    if (mState != State::DELAYED_SRV_DISCOVERY) {
        return;
    }

    mStartHandle = foundHandle;
    mEndHandle = endHandle;
    mResolved[mCurrent].startHandle = foundHandle;
    mResolved[mCurrent].endHandle = endHandle;
    mState = State::SRV_DISCOVERED;
    mpSched->arm(kStageTimeoutMs);   // watchdog for the stage-completion event
    LOG_DEBUG("Client %u: service found [0x%04X..0x%04X]\n", mCurrent,
            foundHandle, endHandle);
}

void PeripheralLinkClients::characteristicsDiscovery()
{
    LOG_DEBUG("Client %u: discovering characteristics\n", mCurrent);
    if (mpOps->discoverAllChars(mConnHandle, mStartHandle, mEndHandle)) {
        // Waiting for onCharDiscovered() / onGattError(0x0A).
        return;
    }

    if (++mDiscRetry <= kMaxDiscRetry) {
        LOG_DEBUG("Client %u: characteristic discovery retry %u/%u\n", mCurrent,
                mDiscRetry, kMaxDiscRetry);
        mpSched->arm(kDiscRetryMs);
        return;
    }

    LOG_WARNING("Client %u: characteristic discovery failed\n", mCurrent);
    mDiscRetry = 0;
    mClients[mCurrent]->onServiceUnavailable();
    advance();
}

void PeripheralLinkClients::onCharDiscovered(const uint8_t* attrData, uint8_t len)
{
    if (mState != State::DELAYED_CHR_DISCOVERY &&
            mState != State::CHR_DISCOVERED) {
        return;
    }

    // Only 128-bit characteristics are matched (all Apple services use them).
    if (attrData == nullptr || len != kCharPair128Len) {
        return;
    }

    uint8_t uuid128[16];
    memcpy(uuid128, &attrData[5], sizeof(uuid128));
    Ble::UUID uuid(uuid128);

    IGattClient* client = mClients[mCurrent];
    ResolvedService& rs = mResolved[mCurrent];

    for (uint8_t i = 0; i < client->charCount(); i++) {
        if (uuid != client->charUuid(i)) {
            continue;
        }
        if (rs.charCount < ResolvedService::kMaxChars) {
            ResolvedChar& rc = rs.chars[rs.charCount++];
            rc.uuid = uuid;
            memcpy(&rc.declHandle, &attrData[0], 2);
            rc.props = attrData[2];
            memcpy(&rc.valueHandle, &attrData[3], 2);
            // Fallback CCCD position; corrected by descriptor discovery below.
            rc.cccdHandle = rc.valueHandle + 1;
            rc.cccdFromDescriptor = false;
            LOG_DEBUG("Client %u: characteristic found, valueHandle=0x%04X\n",
                    mCurrent, rc.valueHandle);
        }
        mState = State::CHR_DISCOVERED;
        mpSched->arm(kStageTimeoutMs);   // watchdog for the stage-completion event
        break;
    }
}

void PeripheralLinkClients::descriptorDiscovery()
{
    LOG_DEBUG("Client %u: discovering descriptors\n", mCurrent);
    if (!mpOps->discoverAllCharDescriptors(mConnHandle, mStartHandle,
            mEndHandle)) {
        if (++mDiscRetry <= kMaxDiscRetry) {
            LOG_DEBUG("Client %u: descriptor discovery retry %u/%u\n", mCurrent,
                    mDiscRetry, kMaxDiscRetry);
            mpSched->arm(kDiscRetryMs);
            return;
        }
        // Give up: resolve with the fallback CCCD positions (value + 1).
        LOG_WARNING("Client %u: descriptor discovery failed\n", mCurrent);
        mDiscRetry = 0;
        resolveCurrent();
        return;
    }

    mInDescDiscovery = true;
    mpSched->arm(kStageTimeoutMs);   // watchdog: proc-complete may be delayed/dropped

    // Waiting for:
    //   onCharDescFound()      -> one descriptor per call (CCCDs captured)
    //   onGattProcComplete()   -> discovery stage complete (no 0x0A is sent)
}

void PeripheralLinkClients::onGattProcComplete(uint8_t errorCode)
{
    (void)errorCode;

    // Descriptor discovery completes with a procedure-complete event rather than
    // the ATT 0x0A used by the other stages.
    if (mInDescDiscovery) {
        resolveCurrent();
        return;
    }

    // Service / characteristic discovery also raise procedure-complete; the ATT
    // 0x0A that drives onGattError() is not always delivered (it can be lost
    // under heavy link traffic). Advance the "found" stages here too so discovery
    // does not have to wait for the watchdog. A no-op in any other state.
    completeFoundStage();
}

void PeripheralLinkClients::onCharDescFound(uint16_t attrHandle, uint16_t uuid16)
{
    if (mState != State::DELAYED_DESC_DISCOVERY || uuid16 != kCccdUuid) {
        return;
    }

    // Assign the CCCD to the characteristic whose value handle most closely
    // precedes it (descriptors follow their characteristic's value).
    ResolvedService& rs = mResolved[mCurrent];
    ResolvedChar* owner = nullptr;
    for (uint8_t i = 0; i < rs.charCount; i++) {
        if (rs.chars[i].valueHandle < attrHandle &&
                (owner == nullptr ||
                        rs.chars[i].valueHandle > owner->valueHandle)) {
            owner = &rs.chars[i];
        }
    }
    if (owner != nullptr) {
        owner->cccdHandle = attrHandle;
        owner->cccdFromDescriptor = true;
        LOG_DEBUG("Client %u: CCCD 0x%04X for valueHandle=0x%04X\n", mCurrent,
                attrHandle, owner->valueHandle);
    }
}

void PeripheralLinkClients::onGattError(uint8_t reqOpcode, uint16_t attrHandle,
        uint8_t errorCode)
{
    // The end-of-stage marker (Attribute Not Found) drives discovery; any other
    // GATT error (e.g. a rejected subscribe or write) is surfaced for diagnosis
    // rather than silently dropped.
    if (errorCode != kAttErrNotFound) {
        LOG_WARNING("GATT error: opcode=0x%02X attr=0x%04X code=0x%02X\n",
                reqOpcode, attrHandle, errorCode);
        return;
    }

    switch (mState) {
        case State::DELAYED_SRV_DISCOVERY:
            // Service not found on this link (the client logs the named result).
            LOG_DEBUG("Client %u: service not found\n", mCurrent);
            mClients[mCurrent]->onServiceUnavailable();
            advance();
            break;
        case State::DELAYED_CHR_DISCOVERY:
            // No requested characteristic present (client logs the named result).
            LOG_DEBUG("Client %u: no matching characteristics\n", mCurrent);
            mClients[mCurrent]->onServiceUnavailable();
            advance();
            break;
        default:
            // SRV_DISCOVERED -> characteristics, CHR_DISCOVERED -> descriptors.
            // Descriptor discovery ends via onGattProcComplete(), not 0x0A.
            completeFoundStage();
            break;
    }
}

bool PeripheralLinkClients::completeFoundStage()
{
    switch (mState) {
        case State::SRV_DISCOVERED:
            mDiscRetry = 0;
            mState = State::DELAYED_CHR_DISCOVERY;
            mpSched->arm(kStageMs);
            return true;
        case State::CHR_DISCOVERED:
            // Characteristics found; resolve their CCCD handles next.
            mDiscRetry = 0;
            mState = State::DELAYED_DESC_DISCOVERY;
            mpSched->arm(kStageMs);
            return true;
        default:
            return false;
    }
}

void PeripheralLinkClients::resolveCurrent()
{
    mInDescDiscovery = false;
    mState = State::AWAIT_CLIENT_READY;
    // Watchdog: if the client's setup stalls and it never calls clientReady(),
    // the next tick skips it (see onTick). A client that reports ready re-arms
    // the timer for the next stage, superseding this.
    mpSched->arm(kClientReadyTimeoutMs);
    LOG_DEBUG("Client %u: resolved %u characteristic(s)\n", mCurrent,
            mResolved[mCurrent].charCount);
    // The client performs its setup writes and calls clientReady() when done.
    mClients[mCurrent]->onServicesResolved(mResolved[mCurrent]);
}

void PeripheralLinkClients::onNotification(uint16_t attrHandle, const uint8_t* data,
        uint8_t len)
{
    for (uint8_t i = 0; i < mClientCount; i++) {
        const ResolvedService& rs = mResolved[i];
        for (uint8_t j = 0; j < rs.charCount; j++) {
            if (rs.chars[j].valueHandle != 0 &&
                    rs.chars[j].valueHandle == attrHandle) {
                LOG_DEBUG("Notify handle=0x%04X len=%u -> client %u\n",
                        attrHandle, len, i);
                mClients[i]->onNotification(attrHandle, data, len);
                return;
            }
        }
    }

    LOG_WARNING("Notify handle=0x%04X len=%u not routed to any client\n",
            attrHandle, len);
}

} // namespace Ble
