/**
 ******************************************************************************
 * @file    AccessoryScanner.hpp
 * @brief   Single owner of the GAP discovery scan for accessory acquisition.
 ******************************************************************************
 *
 * GAP discovery is a global, single-instance radio resource: one procedure at a
 * time, and it must be terminated before a connection is initiated. With one
 * accessory link that constraint was implicit in AccessoryCentral's state
 * machine; with several links it has to be explicit, or two links racing to scan
 * would each think they owned the radio. This class is that single owner.
 *
 * It holds the SET of service UUIDs currently wanted, starts and stops the scan
 * through IGapScanOps, filters advertising reports, and reports each match with
 * the UUID that matched -- so a caller hunting HR, power and speed/cadence in one
 * burst learns WHICH kind it found and can route it to the right link slot.
 *
 * DELIBERATELY NOT HERE (this is a behaviour-neutral extraction):
 *  - timing. No timer is owned; the caller decides when to start and stop a
 *    burst. Burst length, inter-burst gap, the fast-burst/slow-retry ladder and
 *    the acquire delay all remain scheduling policy above this class.
 *  - budgets and back-off. Scan-cycle counting, the terminal NotFound decision
 *    and the escalating scan-start back-off stay with the caller; they are
 *    acquisition policy, not scanning mechanics.
 *  - the phone-transfer coexistence gate, for the same reason.
 * Those move to the supervisor, where they belong beside slot policy.
 * Splitting them out here as well would have made this step a rewrite rather
 * than an extraction, and the timings encode hard-won constraints (the scan
 * back-off exists because a wedged controller starves the event pump; the
 * acquire delay exists because a scan racing app GUI-init caused a watchdog
 * reset). Those are safer moved once, deliberately, than shaken loose here.
 *
 ******************************************************************************
 */

#ifndef ACCESSORY_SCANNER_HPP
#define ACCESSORY_SCANNER_HPP

#include <cstdint>

#include "IGapScanOps.hpp"

namespace Ble
{

/**
 * @brief   Owns the one GAP discovery scan and matches advertisers against a
 *          set of wanted service UUIDs.
 */
class AccessoryScanner {
public:

    /// Most service UUIDs that can be hunted at once: HRS + CPS + CSC, with
    /// headroom for FTMS. Matches the accessory-kind ceiling, not the link count.
    static constexpr uint8_t kMaxWantedUuids = 4;

    /**
     * @brief   One advertiser seen during a scan that matched a wanted UUID.
     */
    struct Match {
        uint8_t  addrType;
        uint8_t  addr[6];
        int8_t   rssi;
        uint16_t matchedUuid;   ///< which wanted service this advertiser offers
        char     name[24];      ///< advertised local name; empty if not present
    };

    /**
     * @brief   Sink for scan matches.
     */
    class MatchCallback {
    public:
        virtual void onScanMatch(const Match &match) = 0;
    protected:
        virtual ~MatchCallback() = default;
    };

    /**
     * @brief   Construct bound to a scan-operations port.
     * @param   ops: GAP scan operations (BlueNRG-backed in the firmware, a mock
     *          in host tests).
     */
    explicit AccessoryScanner(IGapScanOps &ops) : mOps(ops) {}

    /**
     * @brief   Replace the set of service UUIDs to match. Silently ignores
     *          entries beyond kMaxWantedUuids and duplicates.
     */
    void clearWantedUuids() { mWantedCount = 0; }
    bool addWantedUuid(uint16_t uuid);
    uint8_t wantedCount() const { return mWantedCount; }

    /**
     * @brief   Start a scan burst.
     * @param   cb: sink for matches; may be nullptr to scan without reporting.
     * @retval  'false' if a scan is already running (the resource is taken) or
     *          the controller rejected the procedure. On rejection the scanner
     *          stays stopped, so the caller's back-off logic sees a clean state.
     */
    bool start(MatchCallback *cb);

    /**
     * @brief   Stop the running scan. Safe to call when not scanning.
     * @retval  'true' if a scan was running and the controller accepted the stop.
     */
    bool stop();

    /**
     * @brief   Whether a scan is currently running. The single-owner guard: a
     *          caller must not initiate a connection while this is true.
     */
    bool isScanning() const { return mScanning; }

    /// una_ble_hci_le_advertising_report_event Event_Type values we care about.
    static constexpr uint8_t kEvtAdvInd  = 0x00;
    static constexpr uint8_t kEvtScanRsp = 0x04;

    /**
     * @brief   Wait for a scan response before reporting a nameless match.
     * @param   on: enable name completion.
     * @note    OFF by default, and deliberately so. Acquisition connects by
     *          ADDRESS and never needs a name, so holding a match there would
     *          delay a connection to buy nothing. Only the settings scan, whose
     *          entire purpose is a human-readable list, turns this on.
     * @note    A peer whose Local Name is only in its SCAN_RSP (the Polar H10)
     *          otherwise lists as "Unknown": the response carries no service
     *          UUID, so it is dropped by the wanted-UUID filter before its name
     *          can be read.
     */
    void setAwaitNames(bool on) { mAwaitNames = on; }

    /**
     * @brief   Feed an advertising report. Ignored unless scanning. Reports a
     *          match to the callback when the payload lists a wanted UUID.
     * @param   eventType: advertising report Event_Type; kEvtScanRsp marks a
     *          scan response, which is exempt from the wanted-UUID filter when
     *          it belongs to an address already matched in this burst.
     * @retval  'true' if the report matched and was dispatched or held.
     */
    bool onAdvReport(uint8_t addrType, const uint8_t addr[6], const uint8_t *data,
            uint8_t dataLen, int8_t rssi, uint8_t eventType = kEvtAdvInd);

    /**
     * @brief   Note that the controller reported the discovery procedure has
     *          ended by itself (GAP proc-complete), so the scanner is no longer
     *          holding the radio. Keeps isScanning() honest without a stop().
     */
    void onDiscoveryComplete()
    {
        mScanning = false;
        // Flush too: the controller ending the procedure by itself is still the
        // end of the burst, and held matches would otherwise be zeroed by the
        // next start() -- losing a device that only advertised in this burst.
        flushPending();
        mpCb = nullptr;
    }

private:

    // Scan: ~100 ms interval / 40 ms window. The 40 ms window roughly doubles
    // the per-burst catch rate over the original 15 ms. The window is not the
    // limiting factor -- 60 ms ran 150 s stable in isolation; the historical
    // watchdog reset was a wide scan racing app GUI-init, not report flooding.
    static constexpr uint16_t kScanInterval = 0x00A0;
    static constexpr uint16_t kScanWindow   = 0x0040;  // 40 ms

    /// Matches held back waiting for a scan response to supply a name. Sized for
    /// a settings scan in a populated room; overflow simply reports immediately,
    /// which is the pre-existing behaviour rather than a loss.
    static constexpr uint8_t kMaxPending = 8;

    void emitMatch(const Match &m);
    int8_t findPending(const uint8_t addr[6]) const;
    void removePending(uint8_t idx);
    void flushPending(MatchCallback *cb);

public:
    /**
     * @brief   Report every held match now, with whatever name it has.
     * @note    Called when a burst ends so a peer that never sent a scan
     *          response -- or sent one with no name -- still appears, just at
     *          the end of the burst rather than the instant it was seen.
     */
    void flushPending();

private:

    IGapScanOps &mOps;
    MatchCallback *mpCb { nullptr };
    bool mScanning { false };
    bool mAwaitNames { false };

    uint16_t mWanted[kMaxWantedUuids] {};
    uint8_t  mWantedCount { 0 };

    Match   mPending[kMaxPending] {};
    uint8_t mPendingCount { 0 };
};

}  /* namespace Ble */

#endif /* ACCESSORY_SCANNER_HPP */
