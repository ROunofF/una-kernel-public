/**
 ******************************************************************************
 * @file    AccessoryScanner.cpp
 * @brief   Single owner of the GAP discovery scan for accessory acquisition.
 ******************************************************************************
 *
 * No BlueNRG/OS dependencies: the controller calls go through IGapScanOps, so
 * this is host-unit-tested against a mock port.
 *
 ******************************************************************************
 */

#include "AccessoryScanner.hpp"

#include <cstring>

#include "AccessoryAdvParser.hpp"

namespace Ble
{

bool AccessoryScanner::addWantedUuid(uint16_t uuid)
{
    for (uint8_t i = 0; i < mWantedCount; ++i) {
        if (mWanted[i] == uuid) {
            return true;   // already wanted; idempotent
        }
    }
    if (mWantedCount >= kMaxWantedUuids) {
        return false;
    }
    mWanted[mWantedCount++] = uuid;
    return true;
}

bool AccessoryScanner::start(MatchCallback *cb)
{
    // Single-owner guard: the radio can only run one discovery procedure, so a
    // second start is a caller bug rather than something to paper over.
    if (mScanning) {
        return false;
    }
    // Scanning for nothing would burn radio time and report nothing.
    if (mWantedCount == 0) {
        return false;
    }

    // filterDuplicates ON: repeat reports otherwise back the controller event
    // queue up into aci_blue_events_lost.
    if (!mOps.startGeneralDiscovery(kScanInterval, kScanWindow, true)) {
        // Leave the scanner stopped so the caller's back-off sees a clean state.
        return false;
    }

    mpCb = cb;
    mScanning = true;
    mPendingCount = 0;   // held matches belong to one burst only
    return true;
}

bool AccessoryScanner::stop()
{
    if (!mScanning) {
        return false;
    }
    // Clear ownership BEFORE flushing. A match callback may legitimately call
    // stop() again -- settings stops scanning once the device it wanted
    // appears -- and if mScanning were still set that re-entrant call would
    // pass the guard, issue a SECOND terminate, and null mpCb underneath the
    // outer flush, stranding every match still to be emitted.
    //
    // Drop the flag regardless of what the controller says below: if it refused
    // the termination the procedure is in an unknown state, and claiming to
    // still own the radio would block the caller from ever recovering. The
    // caller treats a false return as "verify/reset", not "keep scanning".
    MatchCallback *cb = mpCb;
    mScanning = false;
    mpCb = nullptr;

    // Flush through the captured callback, or a peer held for a scan response
    // that never came would be silently lost at the end of every burst.
    flushPending(cb);

    return mOps.terminateGeneralDiscovery();
}

void AccessoryScanner::emitMatch(const Match &m)
{
    if (mpCb != nullptr) {
        mpCb->onScanMatch(m);
    }
}

int8_t AccessoryScanner::findPending(const uint8_t addr[6]) const
{
    for (uint8_t i = 0; i < mPendingCount; ++i) {
        if (memcmp(mPending[i].addr, addr, 6) == 0) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

bool AccessoryScanner::onAdvReport(uint8_t addrType, const uint8_t addr[6],
        const uint8_t *data, uint8_t dataLen, int8_t rssi, uint8_t eventType)
{
    if (!mScanning || addr == nullptr) {
        return false;
    }

    // A scan response is exempt from the wanted-UUID filter, but only for an
    // address this burst already matched. That exemption is the whole fix: a
    // response carrying just a Local Name has no service UUID, so the filter
    // would drop it and the peer would list as "Unknown" forever.
    if (mAwaitNames && eventType == kEvtScanRsp) {
        const int8_t idx = findPending(addr);
        if (idx >= 0) {
            char name[sizeof(Match::name)] {};
            parseAdvName(data, dataLen, name, sizeof(name));
            if (name[0] == 0) {
                return true;    // still nothing to add; keep waiting
            }
            Match m = mPending[idx];
            memcpy(m.name, name, sizeof(m.name));
            // Remove before emitting: the callback may re-enter (stop()).
            removePending(static_cast<uint8_t>(idx));
            emitMatch(m);
            return true;
        }
        // NOT held -- fall through to the UUID filter rather than discarding.
        // A device whose ADV payload is full may carry its service list in the
        // scan response instead, and dropping those here would make them
        // invisible in the settings list while still connectable during
        // acquisition (where name completion is off and the filter runs).
    }

    uint16_t matched = 0;
    if (!advertisesAnyService(data, dataLen, mWanted, mWantedCount, &matched)) {
        return false;
    }

    Match m {};
    m.addrType = addrType;
    memcpy(m.addr, addr, sizeof(m.addr));
    m.rssi = rssi;
    m.matchedUuid = matched;
    parseAdvName(data, dataLen, m.name, sizeof(m.name));

    // Report at once when we already have a name, or when the caller does not
    // care about names (acquisition connects by address). Holding otherwise
    // would delay a connection to buy nothing.
    if (!mAwaitNames || m.name[0] != 0) {
        // Discard any nameless entry held for this address first, or it would
        // be emitted again at burst end as a duplicate row. Only reachable if
        // the controller's duplicate filter lets a second UNA_BLE_ADV_IND through,
        // which its table can do when it overflows in a crowded room.
        const int8_t held = findPending(addr);
        if (held >= 0) {
            removePending(static_cast<uint8_t>(held));
        }
        emitMatch(m);
        return true;
    }

    // Nameless: hold it until the scan response arrives, or until the burst
    // ends. Already held, or no room -- report now rather than lose it.
    if (findPending(addr) >= 0) {
        return true;
    }
    if (mPendingCount >= kMaxPending) {
        emitMatch(m);
        return true;
    }
    mPending[mPendingCount++] = m;
    return true;
}

void AccessoryScanner::removePending(uint8_t idx)
{
    if (idx >= mPendingCount) {
        return;   // already gone; never underflow the count
    }
    mPending[idx] = mPending[mPendingCount - 1];
    --mPendingCount;
}

void AccessoryScanner::flushPending()
{
    flushPending(mpCb);
}

void AccessoryScanner::flushPending(MatchCallback *cb)
{
    // A peer that never sends a scan response, or whose response carries no
    // name, must still appear -- just at the end of the burst rather than the
    // instant it was seen.
    //
    // The count is snapshotted and zeroed before any callback runs, so a
    // callback that re-enters the scanner cannot see a half-emptied table.
    const uint8_t count = mPendingCount;
    mPendingCount = 0;
    if (cb == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        cb->onScanMatch(mPending[i]);
    }
}

}  /* namespace Ble */
