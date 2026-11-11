/**
 ******************************************************************************
 * @file    AirohaAiding.hpp
 * @brief   Helper class for retrieving time, location andEPO files from the
 *          file system.
 ******************************************************************************
 *
 ******************************************************************************
 */

#pragma once

#include <ctime>
#include "IFileSystem.hpp"
#include "IRtc.hpp"
#include "OS.hpp"

#include "AG3335/AirohaType.hpp"
#include "AG3335/AirohaBase.hpp"

namespace Airoha
{

class Aiding : public AirohaBase::AidingCallback
{

public:

    struct EpoPeriod {
        struct Range {
            uint32_t start;
            uint32_t end;
        };
        Range gps;
        Range glonass;
        Range galileo;
        Range beidou;
        Range qzss;
    };

    Aiding() = default;
    Aiding(Interface::IRtc &rtc, Interface::IFileSystem &fs, const char* epoDir);
    virtual ~Aiding();
    bool config(Interface::IRtc &rtc, Interface::IFileSystem &fs, const char* epoDir);

    EpoPeriod getEpoTimeRange();

    void setReferenceLocation(float lat, float lon, float alt);
    bool getReferenceLocation(float& lat, float& lon, float& alt);

protected:

private:
    /// How long an EPO scan will wait for another task's scan to finish.
    /// Bounded rather than osWaitForever to cap latency, not to protect the
    /// watchdog: the IWDG is fed from vApplicationIdleHook, so a task blocked
    /// on a mutex yields to idle and cannot trip it. What a long wait would
    /// actually cost is responsiveness — the BLE HCI task stops servicing
    /// events, and the Airoha worker starts dropping NMEA sentences once its
    /// queue backs up. An unanswered EPO query is the cheaper failure.
    static constexpr uint32_t kEpoScanTimeoutMs = 5000;

    Interface::IRtc *mRtc = nullptr;
    Interface::IFileSystem *mFs = nullptr;

    /// Serialises the two EPO scans below. They share every handle that
    /// follows, and they are reached from THREE tasks: the Airoha worker
    /// (aiding requests from the GPS driver), the BLE HCI task (the phone's
    /// EPO-status query) and the Backend task (once at boot, and again
    /// whenever the GUI asks for the EPO range). FatFs being re-entrant does
    /// not cover this — its volume mutex protects FatFs' own internals, not
    /// our DIR and FIL objects, so one task repathing/reopening a handle
    /// while another is blocked inside FatFs on that same handle corrupts it
    /// under the blocked task's feet.
    OS::Mutex mEpoScanLock;

    std::unique_ptr<Interface::IDirectory> mDir;
    Interface::IFileSystem::ObjectInfo mItem;
    std::unique_ptr<Interface::IFile> mFile;

    /// What every EPO file is read through, 16 segments at a time. Shared by
    /// both scans, which is safe because both hold @ref mEpoScanLock. A member
    /// rather than an allocation so the scans never fail for want of it: the
    /// files used to be read whole into a buffer the size of the largest one,
    /// 64 KB from the kernel heap, which a large app left no room for.
    uint8_t mScratch[16 * EpoData::kSegmentSize] {};

    virtual bool onAidingRefTimeRequest(std::tm &utc) override;
    virtual bool onAidingRefLocationRequest(ReferenceLocation &loc) override;
    virtual void onAidingRefLocationSave(float lat, float lon, float alt) override;
    virtual EpoData onAidingEpoRequest(SystemType sys, uint32_t week, uint32_t tow) override;

    /// Body of getEpoTimeRange(). Touches mDir/mItem/mFile, so the caller must
    /// hold @ref mEpoScanLock.
    EpoPeriod scanEpoTimeRangeLocked();

    /// Body of onAidingEpoRequest(). Touches mDir/mItem/mFile, so the caller
    /// must hold @ref mEpoScanLock.
    EpoData readEpoFileLocked(SystemType sys, uint32_t week, uint32_t tow);

};

}  /* namespace Airoha */

