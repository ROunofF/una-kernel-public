/**
 ******************************************************************************
 * @file    MtpResponder.cpp
 * @brief   MTP responder over FatFs. See MtpResponder.hpp.
 *
 *          Protocol: PIMA 15740 (PTP) with the MTP 1.1 extensions, USB
 *          Still Image transport. One storage, the user volume. Objects are
 *          the files and folders on it; a handle is an index into a table
 *          of (parent, name) pairs that grows as the host browses.
 ******************************************************************************
 */

#include "MtpResponder.hpp"

#include <cstring>
#include <ctime>
#include <new>

#include "main.h"
#include "usbd_mtp.h"

#define LOG_MODULE_PRX      "Mtp"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace {

enum : uint16_t {
    OP_GET_DEVICE_INFO            = 0x1001,
    OP_OPEN_SESSION               = 0x1002,
    OP_CLOSE_SESSION              = 0x1003,
    OP_GET_STORAGE_IDS            = 0x1004,
    OP_GET_STORAGE_INFO           = 0x1005,
    OP_GET_NUM_OBJECTS            = 0x1006,
    OP_GET_OBJECT_HANDLES         = 0x1007,
    OP_GET_OBJECT_INFO            = 0x1008,
    OP_GET_OBJECT                 = 0x1009,
    OP_DELETE_OBJECT              = 0x100B,
    OP_SEND_OBJECT_INFO           = 0x100C,
    OP_SEND_OBJECT                = 0x100D,
    OP_GET_DEVICE_PROP_DESC       = 0x1014,
    OP_GET_DEVICE_PROP_VALUE      = 0x1015,
    OP_MOVE_OBJECT                = 0x1019,
    OP_GET_PARTIAL_OBJECT         = 0x101B,
    OP_GET_OBJECT_PROPS_SUPPORTED = 0x9801,
    OP_GET_OBJECT_PROP_DESC       = 0x9802,
    OP_GET_OBJECT_PROP_VALUE      = 0x9803,
    OP_SET_OBJECT_PROP_VALUE      = 0x9804,
    OP_GET_OBJECT_PROP_LIST       = 0x9805,
    OP_GET_OBJECT_REFERENCES      = 0x9810,
};

enum : uint16_t {
    RC_OK                         = 0x2001,
    RC_GENERAL_ERROR              = 0x2002,
    RC_SESSION_NOT_OPEN           = 0x2003,
    RC_OPERATION_NOT_SUPPORTED    = 0x2005,
    RC_INCOMPLETE_TRANSFER        = 0x2007,
    RC_INVALID_STORAGE_ID         = 0x2008,
    RC_INVALID_OBJECT_HANDLE      = 0x2009,
    RC_DEVICE_PROP_NOT_SUPPORTED  = 0x200A,
    RC_STORE_FULL                 = 0x200C,
    RC_ACCESS_DENIED              = 0x200F,
    RC_PARTIAL_DELETION           = 0x2012,
    RC_NO_VALID_OBJECT_INFO       = 0x2015,
    RC_DEVICE_BUSY                = 0x2019,
    RC_INVALID_PARENT_OBJECT      = 0x201A,
    RC_INVALID_PARAMETER          = 0x201D,
    RC_SESSION_ALREADY_OPEN       = 0x201E,
    RC_INVALID_OBJECT_PROP_CODE   = 0xA801,
    RC_INVALID_DATASET            = 0xA806,
    RC_GROUP_UNSUPPORTED          = 0xA807,
    RC_DEPTH_UNSUPPORTED          = 0xA808,
    RC_OBJECT_PROP_NOT_SUPPORTED  = 0xA80A,
};

enum : uint16_t { CT_COMMAND = 1, CT_DATA = 2, CT_RESPONSE = 3, CT_EVENT = 4 };

enum : uint16_t {
    EV_OBJECT_ADDED         = 0x4002,
    EV_OBJECT_REMOVED       = 0x4003,
    EV_OBJECT_INFO_CHANGED  = 0x4007,
};

enum : uint16_t { FMT_UNDEFINED = 0x3000, FMT_ASSOCIATION = 0x3001 };

enum : uint16_t {
    PROP_STORAGE_ID         = 0xDC01,
    PROP_OBJECT_FORMAT      = 0xDC02,
    PROP_PROTECTION_STATUS  = 0xDC03,
    PROP_OBJECT_SIZE        = 0xDC04,
    PROP_OBJECT_FILE_NAME   = 0xDC07,
    PROP_DATE_CREATED       = 0xDC08,
    PROP_DATE_MODIFIED      = 0xDC09,
    PROP_PARENT_OBJECT      = 0xDC0B,
    PROP_PUOID              = 0xDC41,
    PROP_NAME               = 0xDC44,
};

enum : uint16_t {
    DT_UINT16  = 0x0004,
    DT_UINT32  = 0x0006,
    DT_UINT64  = 0x0008,
    DT_UINT128 = 0x000A,
    DT_STR     = 0xFFFF,
};

constexpr uint16_t DPROP_FRIENDLY_NAME = 0xD402;
constexpr uint16_t ASSOC_GENERIC_FOLDER = 0x0001;

constexpr uint32_t kHeader     = 12;
constexpr uint32_t kAll        = 0xFFFFFFFF;
constexpr uint32_t kTxTimeout  = 10000;
constexpr uint32_t kRxTimeout  = 10000;

constexpr uint32_t kFlagRx     = 1u << 0;
constexpr uint32_t kFlagTx     = 1u << 1;
constexpr uint32_t kFlagCfgOn  = 1u << 2;
constexpr uint32_t kFlagCfgOff = 1u << 3;
constexpr uint32_t kFlagCancel = 1u << 4;
constexpr uint32_t kFlagAbort  = kFlagCfgOn | kFlagCfgOff | kFlagCancel;
constexpr uint32_t kFlagStale  = 1u << 5;    // Never set; waitEvents()' "woke, look again"
constexpr uint32_t kFlagEvt    = 1u << 6;

// How often, while idle, the folders the host has listed are read again for
// changes the watch made. Only while a session is open.
constexpr uint32_t kPollMs     = 2000;
// While the host has the bus suspended, how often to look whether it is back.
constexpr uint32_t kResumeMs   = 250;
// An event goes out at the host's next interrupt poll (16 ms); longer means
// it is not reading them.
constexpr uint32_t kEvtWaitMs  = 100;

constexpr uint8_t  kObjDir    = 0x01;
constexpr uint8_t  kObjGone   = 0x02;
constexpr uint8_t  kObjListed = 0x04;   // A folder whose children the host has listed
constexpr uint8_t  kObjSeen   = 0x08;   // Found by the poll in progress

/* What the host was told of a file, folded into 32 bits; 0 means "not told". */
inline uint32_t stampOf(uint64_t size, uint16_t fdate, uint16_t ftime)
{
    uint32_t s = ((uint32_t)size * 2654435761u) ^ (uint32_t)(size >> 32) ^
                 (((uint32_t)fdate << 16) | ftime);
    return (s == 0) ? 1 : s;
}

/* A FAT date and time, stamped in UTC by get_fattime(), as seconds since the
   epoch. Not FS::FileSystem::FatToUtc(): that one uses mktime(), which reads
   the fields as local time, so its result is off by the time zone. */
inline time_t fatToEpoch(uint16_t fdate, uint16_t ftime)
{
    // Days from 1970-01-01 to a civil date (H. Hinnant's days_from_civil).
    int32_t y = 1980 + (fdate >> 9);
    const uint32_t m = (fdate >> 5) & 0xF;
    const uint32_t d = fdate & 0x1F;
    y -= (m <= 2) ? 1 : 0;
    const int32_t  era = ((y >= 0) ? y : (y - 399)) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);
    const uint32_t doy = (153 * ((m > 2) ? (m - 3) : (m + 9)) + 2) / 5 + d - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return (time_t)(days * 86400 + (ftime >> 11) * 3600 + ((ftime >> 5) & 0x3F) * 60 +
                    (ftime & 0x1F) * 2);
}

const uint16_t kEvents[] = {
    EV_OBJECT_ADDED, EV_OBJECT_REMOVED, EV_OBJECT_INFO_CHANGED,
};

const uint16_t kOperations[] = {
    OP_GET_DEVICE_INFO, OP_OPEN_SESSION, OP_CLOSE_SESSION,
    OP_GET_STORAGE_IDS, OP_GET_STORAGE_INFO, OP_GET_NUM_OBJECTS,
    OP_GET_OBJECT_HANDLES, OP_GET_OBJECT_INFO, OP_GET_OBJECT,
    OP_DELETE_OBJECT, OP_SEND_OBJECT_INFO, OP_SEND_OBJECT,
    OP_GET_DEVICE_PROP_DESC, OP_GET_DEVICE_PROP_VALUE, OP_MOVE_OBJECT,
    OP_GET_PARTIAL_OBJECT, OP_GET_OBJECT_PROPS_SUPPORTED,
    OP_GET_OBJECT_PROP_DESC, OP_GET_OBJECT_PROP_VALUE,
    OP_SET_OBJECT_PROP_VALUE, OP_GET_OBJECT_PROP_LIST,
    OP_GET_OBJECT_REFERENCES,
};

const uint16_t kObjectProps[] = {
    PROP_STORAGE_ID, PROP_OBJECT_FORMAT, PROP_PROTECTION_STATUS,
    PROP_OBJECT_SIZE, PROP_OBJECT_FILE_NAME, PROP_DATE_CREATED,
    PROP_DATE_MODIFIED, PROP_PARENT_OBJECT, PROP_PUOID, PROP_NAME,
};

const uint16_t kFormats[]    = { FMT_UNDEFINED, FMT_ASSOCIATION };
const uint16_t kDeviceProps[] = { DPROP_FRIENDLY_NAME };

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

uint16_t propType(uint32_t prop)
{
    switch (prop) {
    case PROP_STORAGE_ID:        return DT_UINT32;
    case PROP_OBJECT_FORMAT:     return DT_UINT16;
    case PROP_PROTECTION_STATUS: return DT_UINT16;
    case PROP_OBJECT_SIZE:       return DT_UINT64;
    case PROP_OBJECT_FILE_NAME:  return DT_STR;
    case PROP_DATE_CREATED:      return DT_STR;
    case PROP_DATE_MODIFIED:     return DT_STR;
    case PROP_PARENT_OBJECT:     return DT_UINT32;
    case PROP_PUOID:             return DT_UINT128;
    case PROP_NAME:              return DT_STR;
    default:                     return 0;
    }
}

inline uint16_t get16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
inline uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
inline void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

uint16_t mapResult(FRESULT res)
{
    switch (res) {
    case FR_OK:                 return RC_OK;
    case FR_NO_FILE:
    case FR_NO_PATH:            return RC_INVALID_OBJECT_HANDLE;
    case FR_LOCKED:
    case FR_TIMEOUT:            return RC_DEVICE_BUSY;
    case FR_DENIED:
    case FR_EXIST:
    case FR_WRITE_PROTECTED:
    case FR_INVALID_NAME:       return RC_ACCESS_DENIED;
    default:                    return RC_GENERAL_ERROR;
    }
}

/* A 32-character serial from the MCU unique ID, as MTP recommends.
   Hosts use it to tell one device from another. */
void makeSerial(char out[33])
{
    static const char kHex[] = "0123456789ABCDEF";
    const uint32_t id[3] = {
        *(const uint32_t *)(UID_BASE),
        *(const uint32_t *)(UID_BASE + 4),
        *(const uint32_t *)(UID_BASE + 8),
    };
    size_t pos = 0;
    for (uint32_t i = 0; i < 3; i++) {
        for (int s = 28; s >= 0; s -= 4) {
            out[pos++] = kHex[(id[i] >> s) & 0xF];
        }
    }
    while (pos < 32) {
        out[pos++] = '0';
    }
    out[32] = '\0';
}

} // namespace

/* ==========================================================================
 * Dataset writer
 *
 * Every dataset is generated twice: once to count its bytes, because the
 * container header that goes first carries the total length, and once to
 * send it. The second pass is capped (and padded) at the first pass's
 * length, so the host always gets exactly what the header announced, even
 * if a file changed in between.
 * ========================================================================== */

class MtpResponder::Writer {
public:
    Writer(MtpResponder &r, bool counting, uint32_t limit = 0) :
        mR(r), mCounting(counting), mLimit(limit) {}

    void u8(uint8_t v)   { put(&v, 1); }
    void u16(uint16_t v) { put(&v, 2); }
    void u32(uint32_t v) { put(&v, 4); }
    void u64(uint64_t v) { put(&v, 8); }

    void array16(const uint16_t *a, uint32_t n)
    {
        u32(n);
        for (uint32_t i = 0; i < n; i++) {
            u16(a[i]);
        }
    }

    /* MTP string: count of UTF-16 units including the terminator, then the
       units. FatFs names are in the OEM code page (FF_LFN_UNICODE == 0). */
    void str(const char *s, size_t len)
    {
        if (len > 254) {
            len = 254;
        }
        if (len == 0) {
            u8(0);
            return;
        }
        u8((uint8_t)(len + 1));
        for (size_t i = 0; i < len; i++) {
            uint16_t c = (uint8_t)s[i];
            if (c >= 0x80) {
                c = ff_oem2uni(c, FF_CODE_PAGE);
                if (c == 0) {
                    c = '?';
                }
            }
            u16(c);
        }
        u16(0);
    }

    void str(const char *s) { str(s, strlen(s)); }

    /* "YYYYMMDDThhmmss" in the watch's local time, always 15 characters so
       both passes agree. The kernel stamps files in UTC (get_fattime() in
       FileSystem.cpp), but hosts read an MTP date as local time: Windows
       ignores a trailing Z. Phones send local time for the same reason. */
    void date(const Info &info)
    {
        char d[16];
        uint32_t y = 1980, mo = 1, da = 1, h = 0, mi = 0, se = 0;
        if (info.ok && (((info.fdate >> 5) & 0xF) != 0) && ((info.fdate & 0x1F) != 0)) {
            const time_t utc = fatToEpoch(info.fdate, info.ftime);
            std::tm t{};
            if (localtime_r(&utc, &t) != nullptr) {
                y  = (uint32_t)t.tm_year + 1900;
                mo = (uint32_t)t.tm_mon + 1;
                da = (uint32_t)t.tm_mday;
                h  = (uint32_t)t.tm_hour;
                mi = (uint32_t)t.tm_min;
                se = (uint32_t)t.tm_sec;
            }
        }
        const uint32_t v[6] = { y, mo, da, h, mi, se };
        const uint8_t  w[6] = { 4, 2, 2, 2, 2, 2 };
        size_t pos = 0;
        for (int f = 0; f < 6; f++) {
            if (f == 3) {
                d[pos++] = 'T';
            }
            uint32_t x = v[f];
            for (int k = w[f] - 1; k >= 0; k--) {
                d[pos + k] = (char)('0' + (x % 10));
                x /= 10;
            }
            pos += w[f];
        }
        str(d, pos);
    }

    void put(const void *p, uint32_t n)
    {
        const uint8_t *b = static_cast<const uint8_t *>(p);
        if (mCounting) {
            mSize += n;
            return;
        }
        while ((n > 0) && mOk && (mSize < mLimit)) {
            uint32_t chunk = kBufSize - mPos;
            if (chunk > n) {
                chunk = n;
            }
            if (chunk > mLimit - mSize) {
                chunk = mLimit - mSize;
            }
            if (b != nullptr) {
                memcpy(mR.txBuf() + mPos, b, chunk);
                b += chunk;
            } else {
                memset(mR.txBuf() + mPos, 0, chunk);
            }
            mPos  += chunk;
            mSize += chunk;
            n     -= chunk;
            if (mPos == kBufSize) {
                mOk  = mR.txFlush(mPos);
                mPos = 0;
            }
        }
    }

    bool finish()
    {
        if (mCounting) {
            return true;
        }
        if (mSize < mLimit) {
            put(nullptr, mLimit - mSize);
        }
        if (mOk && (mPos > 0)) {
            mOk  = mR.txFlush(mPos);
            mPos = 0;
        }
        // A transfer that ends on a packet boundary needs a zero-length
        // packet, or the host waits for more.
        if (mOk && ((mLimit % mR.mps()) == 0)) {
            mOk = mR.txZlp();
        }
        return mOk;
    }

    uint32_t size() const { return mSize; }

private:
    MtpResponder &mR;
    bool          mCounting;
    uint32_t      mLimit;
    uint32_t      mSize = 0;
    uint32_t      mPos  = 0;
    bool          mOk   = true;
};

/* ==========================================================================
 * Dataset reader (host to device)
 * ========================================================================== */

struct MtpResponder::Reader {
    const uint8_t *p;
    uint32_t       n;
    uint32_t       pos = 0;
    bool           ok  = true;

    Reader(const uint8_t *data, uint32_t len) : p(data), n(len) {}

    bool get(void *out, uint32_t k)
    {
        if (!ok || (pos + k > n)) {
            ok = false;
            memset(out, 0, k);
            return false;
        }
        memcpy(out, p + pos, k);
        pos += k;
        return true;
    }
    uint8_t  u8()  { uint8_t v;  get(&v, 1); return v; }
    uint16_t u16() { uint16_t v; get(&v, 2); return v; }
    uint32_t u32() { uint32_t v; get(&v, 4); return v; }

    /* To the OEM code page FatFs uses. A character it cannot hold becomes
       '_' rather than failing the whole name. */
    bool str(char *out, size_t cap, size_t &len)
    {
        len = 0;
        const uint8_t count = u8();
        for (uint32_t i = 0; i < count; i++) {
            const uint16_t c = u16();
            if (!ok) {
                return false;
            }
            if (c == 0) {
                continue;
            }
            char ch;
            if (c < 0x80) {
                ch = (char)c;
            } else {
                const WCHAR oem = ff_uni2oem(c, FF_CODE_PAGE);
                ch = ((oem == 0) || (oem > 0xFF)) ? '_' : (char)oem;
            }
            if (len + 1 < cap) {
                out[len++] = ch;
            }
        }
        out[len] = '\0';
        return ok;
    }
};

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

bool MtpResponder::init()
{
    if (!mOpLock.init(LOG_MODULE_PRX)) {
        return false;
    }

    static const USBD_MTP_Callbacks kCallbacks = {
        .configured = cbConfigured,
        .rxDone     = cbRxDone,
        .txDone     = cbTxDone,
        .eventDone  = cbEventDone,
        .cancel     = cbCancel,
    };
    USBD_MTP_SetCallbacks(&kCallbacks);

    if (!mThread.init(LOG_MODULE_PRX, this, &MtpResponder::thread,
                      osPriorityBelowNormal)) {
        LOG_ERROR("thread init failed\n");
        return false;
    }
    return true;
}

bool MtpResponder::suspend(uint32_t timeoutMs)
{
    mSuspended = true;
    if (!mOpLock.lock(timeoutMs)) {
        LOG_WARNING("operation still running\n");
        return false;
    }
    mOpLock.release();
    return true;
}

void MtpResponder::resume()
{
    mSuspended = false;
}

bool MtpResponder::allocBuffers()
{
    mBuf = new (std::nothrow) Buffers;
    if (mBuf == nullptr) {
        if (!mOutOfMemory) {
            LOG_ERROR("no memory for MTP (%u bytes)\n", (unsigned)sizeof(Buffers));
        }
        mOutOfMemory = true;
        mStats.allocFails = mStats.allocFails + 1;
        return false;
    }
    mOutOfMemory = false;
    mStats.allocs = mStats.allocs + 1;
    mSession  = 0;
    mTxCur    = 0;
    mListCount = 0;
    resetObjects();
    return true;
}

void MtpResponder::freeBuffers()
{
    delete mBuf;
    mBuf = nullptr;
    mObjCount  = 0;
    mNameUsed  = 0;
    mListCount = 0;
}

uint32_t MtpResponder::heapBytes() const
{
    return (mBuf != nullptr) ? sizeof(Buffers) : 0;
}

void MtpResponder::signal(uint32_t flags)
{
    if (mThreadId != nullptr) {
        (void)osThreadFlagsSet(mThreadId, flags);
    }
}

void MtpResponder::cbConfigured(bool on)
{
    MtpResponder &self = GetInstance();
    // The endpoints were just opened or closed: nothing is queued on them.
    self.mRxArmed = false;
    self.mTxBusy  = false;
    self.mEvtBusy = false;
    self.mOpen    = on;
    self.mCfgGen  = self.mCfgGen + 1;
    if (on) {
        self.mStats.cfgOn = self.mStats.cfgOn + 1;
    } else {
        self.mStats.cfgOff = self.mStats.cfgOff + 1;
    }
    self.signal(on ? kFlagCfgOn : kFlagCfgOff);
}

void MtpResponder::cbRxDone(uint32_t len)
{
    MtpResponder &self = GetInstance();
    self.mStats.rxDone    = self.mStats.rxDone + 1;
    self.mStats.lastRxLen = len;
    self.mRxLen   = len;
    self.mRxArmed = false;
    self.signal(kFlagRx);
}

void MtpResponder::cbTxDone()
{
    MtpResponder &self = GetInstance();
    self.mStats.txDone = self.mStats.txDone + 1;
    self.mTxBusy = false;
    self.signal(kFlagTx);
}

void MtpResponder::cbEventDone()
{
    MtpResponder &self = GetInstance();
    self.mEvtBusy = false;
    self.signal(kFlagEvt);
}

void MtpResponder::cbCancel()
{
    MtpResponder &self = GetInstance();
    self.mStats.cancels = self.mStats.cancels + 1;
    self.signal(kFlagCancel);
}

void MtpResponder::thread()
{
    mThreadId = osThreadGetId();

    for (;;) {
        if (!mOpen) {
            mSession       = 0;
            mPendingHandle = 0;
            // The endpoints are closed by now (USBD_MTP_DeInit closes them
            // before reporting), so nothing can still write the buffers.
            freeBuffers();
            (void)osThreadFlagsWait(kFlagCfgOn, osFlagsWaitAny, osWaitForever);
            continue;
        }

        if ((mBuf == nullptr) && !allocBuffers()) {
            // Apps hold the memory. The host's first command goes
            // unanswered and it gives up; try again in a while, or when the
            // interface comes or goes.
            (void)osThreadFlagsWait(kFlagCfgOn | kFlagCfgOff, osFlagsWaitAny,
                                    pdMS_TO_TICKS(kPollMs));
            continue;
        }

        // Everything from here to the response belongs to the endpoints as
        // they are now. If the interface is reopened meanwhile, the
        // operation is dropped (see waitEvents()).
        mOpGen = mCfgGen;
        mAbort = false;
        uint32_t len = 0;
        if (!rxCommand(len)) {
            // Cancel while idle, or the interface came or went: the loop
            // starts over from the current state.
            USBD_MTP_SetBusy(false);
            continue;
        }

        // A zero-length packet left over from the last data phase, or
        // anything else that is not a command.
        if ((len < kHeader) || (get16(mBuf->rx + 4) != CT_COMMAND)) {
            mStats.notCommand = mStats.notCommand + 1;
            mStats.lastHeader = (len >= 8) ? ((uint32_t)get16(mBuf->rx + 4) << 16) | get16(mBuf->rx + 6)
                                           : 0xFFFF0000u | len;
            continue;
        }

        mStats.commands = mStats.commands + 1;
        mStats.lastOp   = get16(mBuf->rx + 6);
        mCmd.code    = get16(mBuf->rx + 6);
        mCmd.tid     = get32(mBuf->rx + 8);
        mCmd.nparams = (len - kHeader) / 4;
        if (mCmd.nparams > 5) {
            mCmd.nparams = 5;
        }
        for (uint32_t i = 0; i < 5; i++) {
            mCmd.p[i] = (i < mCmd.nparams) ? get32(mBuf->rx + kHeader + 4 * i) : 0;
        }

        USBD_MTP_SetBusy(true);
        if (mOpLock.lock()) {
            dispatch();
            mOpLock.release();
        }
        if (mAbort) {
            abortTransfers();
        }
        USBD_MTP_SetBusy(false);
    }
}

void MtpResponder::abortTransfers()
{
    LOG_INFO("transfer aborted (op 0x%04X)\n", mCmd.code);
    mStats.aborts = mStats.aborts + 1;
    USBD_MTP_AbortBulk();
    mTxBusy  = false;
    mRxArmed = false;
    // Flags raised by what was aborted belong to it.
    (void)osThreadFlagsClear(kFlagRx | kFlagTx | kFlagCancel);
}

uint16_t MtpResponder::mps() const
{
    return USBD_MTP_MaxPacket();
}

/* ==========================================================================
 * Transport
 * ========================================================================== */

uint32_t MtpResponder::waitEvents(uint32_t want, uint32_t timeoutMs)
{
    const uint32_t ticks = (timeoutMs == osWaitForever)
                         ? osWaitForever : pdMS_TO_TICKS(timeoutMs);
    const uint32_t f = osThreadFlagsWait(want | kFlagAbort, osFlagsWaitAny, ticks);
    mStats.lastFlags = f;
    if ((f & 0x80000000u) != 0) {
        mAbort = true;      // Timeout or error
        return 0;
    }
    if ((f & kFlagCancel) != 0) {
        mAbort = true;
        return 0;
    }
    // A configuration flag can be left over from before this operation
    // started: the task only waits for kFlagCfgOn while closed, and
    // osThreadFlagsWait() reports every flag that is set, not only the
    // ones asked for. Only a change since the operation began ends it;
    // the generation tells which. Treating a stale flag as an abort used
    // to drop the command that arrived with it.
    if (mCfgGen != mOpGen) {
        mAbort = true;
        return 0;
    }
    // Whatever was asked for, or only a stale flag: the caller checks its
    // own condition again either way.
    return f | kFlagStale;
}

/* Wait for the next command. Between commands, the folders the host has
   listed are read again every kPollMs, and what changed is reported as
   events: the host caches sizes and dates and would otherwise read a file
   the watch has grown only up to the size it remembers. */
bool MtpResponder::rxCommand(uint32_t &len)
{
    if (mCfgGen != mOpGen) {
        mAbort = true;
        return false;
    }
    if (!mRxArmed) {
        mRxArmed = true;
        mStats.rxArmed = mStats.rxArmed + 1;
        if (!USBD_MTP_Receive(mBuf->rx, mps())) {
            mRxArmed = false;
            mAbort   = true;
            return false;
        }
    }

    while (mRxArmed) {
        uint32_t ticks = osWaitForever;
        if (pollWanted()) {
            if (USBD_MTP_IsBusSuspended()) {
                // Events would wait for the host anyway. Look again soon,
                // so the changes go out as soon as it is back.
                ticks = pdMS_TO_TICKS(kResumeMs);
            } else {
                const uint32_t period = pdMS_TO_TICKS(kPollMs);
                const uint32_t since  = osKernelGetTickCount() - mLastPoll;
                if (since >= period) {
                    pollChanges();
                    mLastPoll = osKernelGetTickCount();
                    continue;
                }
                ticks = period - since;
            }
        }

        const uint32_t f = osThreadFlagsWait(kFlagRx | kFlagAbort, osFlagsWaitAny, ticks);
        if (f == (uint32_t)osFlagsErrorTimeout) {
            continue;
        }
        mStats.lastFlags = f;
        if (((f & 0x80000000u) != 0) || ((f & kFlagCancel) != 0) || (mCfgGen != mOpGen)) {
            mAbort = true;
            return false;
        }
    }
    if (mCfgGen != mOpGen) {
        mAbort = true;
        return false;
    }
    len = mRxLen;
    return true;
}

bool MtpResponder::rxPacket(uint32_t want, uint32_t &len, uint32_t timeoutMs)
{
    if (mCfgGen != mOpGen) {
        mAbort = true;
        return false;
    }
    if (!mRxArmed) {
        mRxArmed = true;
        mStats.rxArmed = mStats.rxArmed + 1;
        if (!USBD_MTP_Receive(mBuf->rx, want)) {
            mRxArmed = false;
            mAbort   = true;
            return false;
        }
    }
    while (mRxArmed) {
        if (waitEvents(kFlagRx, timeoutMs) == 0) {
            if (!mRxArmed) {
                mStats.rxDropped = mStats.rxDropped + 1;
            }
            return false;
        }
    }
    // Reopening the interface also clears mRxArmed: nothing was received.
    if (mCfgGen != mOpGen) {
        mAbort = true;
        return false;
    }
    len = mRxLen;
    return true;
}

bool MtpResponder::txIdle()
{
    while (mTxBusy) {
        if (waitEvents(kFlagTx, kTxTimeout) == 0) {
            return false;
        }
    }
    return true;
}

bool MtpResponder::txSend(const uint8_t *buf, uint32_t len)
{
    if (mAbort || !txIdle()) {
        return false;
    }
    // Reopening the interface also clears mTxBusy; this operation is over.
    if (mCfgGen != mOpGen) {
        mAbort = true;
        return false;
    }
    mTxBusy = true;
    mStats.txStart = mStats.txStart + 1;
    if (!USBD_MTP_Transmit(buf, len)) {
        mStats.txFail = mStats.txFail + 1;
        mTxBusy = false;
        mAbort  = true;
        return false;
    }
    return true;
}

bool MtpResponder::txFlush(uint32_t len)
{
    // Two buffers: the next one is filled while this one goes out.
    const bool ok = txSend(mBuf->tx[mTxCur], len);
    mTxCur ^= 1;
    return ok;
}

bool MtpResponder::txZlp()
{
    return txSend(mResp, 0);
}

void MtpResponder::respond(uint16_t code, uint32_t nparams, uint32_t p0,
                           uint32_t p1, uint32_t p2)
{
    if (mAbort || !txIdle()) {
        return;
    }
    const uint32_t len = kHeader + 4 * nparams;
    put32(mResp + 0, len);
    put16(mResp + 4, CT_RESPONSE);
    put16(mResp + 6, code);
    put32(mResp + 8, mCmd.tid);
    put32(mResp + 12, p0);
    put32(mResp + 16, p1);
    put32(mResp + 20, p2);
    mStats.lastResp = code;
    (void)txSend(mResp, len);
    if (code != RC_OK) {
        LOG_DEBUG("op 0x%04X -> 0x%04X\n", mCmd.code, code);
    }
}

template <typename Gen>
bool MtpResponder::sendData(Gen &&gen)
{
    Writer counter(*this, true);
    gen(counter);
    const uint32_t total = kHeader + counter.size();

    Writer w(*this, false, total);
    w.u32(total);
    w.u16(CT_DATA);
    w.u16(mCmd.code);
    w.u32(mCmd.tid);
    gen(w);
    return w.finish();
}

/* Receive a host-to-device data phase, handing the payload to sink in pieces.
   The whole phase is always read, even after sink fails, so the host is not
   left stuck in the middle of sending. */
template <typename Sink>
bool MtpResponder::rxData(Sink &&sink, bool &complete)
{
    complete = false;
    uint32_t len = 0;
    if (!rxPacket(mps(), len, kRxTimeout)) {
        return false;
    }
    if ((len < kHeader) || (get16(mBuf->rx + 4) != CT_DATA)) {
        LOG_WARNING("expected data phase for op 0x%04X\n", mCmd.code);
        return true;
    }

    const uint32_t total = get32(mBuf->rx);
    uint32_t got = len;
    bool sinkOk = sink(mBuf->rx + kHeader, len - kHeader);

    while (got < total) {
        uint32_t want = total - got;
        want = ((want + mps() - 1) / mps()) * mps();
        if (want > kBufSize) {
            want = kBufSize;
        }
        if (!rxPacket(want, len, kRxTimeout)) {
            return false;
        }
        if (len == 0) {
            break;
        }
        if (sinkOk) {
            sinkOk = sink(mBuf->rx, len);
        }
        got += len;
        if (len < want) {
            break;                  // Short packet: the host ended it here
        }
    }

    complete = sinkOk && (got >= total);
    return true;
}

/* Small datasets (ObjectInfo, a property value) gathered into mBuf->tx[0]. */
bool MtpResponder::rxDataset(uint32_t &len)
{
    len = 0;
    bool fits = true;
    bool complete = false;
    const bool ok = rxData([&](const uint8_t *p, uint32_t n) {
        if (len + n > kBufSize) {
            fits = false;
            return false;
        }
        memcpy(mBuf->tx[0] + len, p, n);
        len += n;
        return true;
    }, complete);
    return ok && fits && complete;
}

/* ==========================================================================
 * Objects
 * ========================================================================== */

void MtpResponder::resetObjects()
{
    mObjCount   = 0;
    mNameUsed   = 0;
    mRootListed = false;
    mAnyListed  = false;
}

uint32_t MtpResponder::findObject(uint32_t parent, const char *name, size_t len) const
{
    for (uint32_t i = 0; i < mObjCount; i++) {
        const Obj &o = mBuf->objs[i];
        if ((o.parent == parent) && ((o.flags & kObjGone) == 0) &&
                (o.nameLen == len) && (memcmp(nameOf(o), name, len) == 0)) {
            return i + 1;
        }
    }
    return 0;
}

void MtpResponder::markListed(uint32_t folder)
{
    if (folder == 0) {
        mRootListed = true;
    } else {
        mBuf->objs[folder - 1].flags |= kObjListed;
    }
    mAnyListed = true;
}

const MtpResponder::Obj *MtpResponder::object(uint32_t handle) const
{
    if ((handle == 0) || (handle > mObjCount)) {
        return nullptr;
    }
    const Obj &o = mBuf->objs[handle - 1];
    return ((o.flags & kObjGone) != 0) ? nullptr : &o;
}

bool MtpResponder::isDir(uint32_t handle) const
{
    const Obj *o = object(handle);
    return (o != nullptr) && ((o->flags & kObjDir) != 0);
}

uint32_t MtpResponder::addObject(uint32_t parent, const char *name, size_t len,
                                 bool dir)
{
    // The same file keeps its handle each time the host lists the folder.
    const uint32_t found = findObject(parent, name, len);
    if (found != 0) {
        Obj &o = mBuf->objs[found - 1];
        o.flags = (uint8_t)(dir ? (o.flags | kObjDir) : (o.flags & ~kObjDir));
        return found;
    }
    if ((mObjCount >= kMaxObjects) || (mNameUsed + len + 1 > kNamePool)) {
        return 0;
    }
    Obj &o = mBuf->objs[mObjCount];
    o.parent  = parent;
    o.nameOff = mNameUsed;
    o.nameLen = (uint16_t)len;
    o.flags   = dir ? kObjDir : 0;
    o.stamp   = 0;
    memcpy(&mBuf->names[mNameUsed], name, len);
    mBuf->names[mNameUsed + len] = '\0';
    mNameUsed += len + 1;
    return ++mObjCount;
}

bool MtpResponder::setName(uint32_t handle, const char *name, size_t len)
{
    if ((handle == 0) || (handle > mObjCount) || (mNameUsed + len + 1 > kNamePool)) {
        return false;
    }
    Obj &o = mBuf->objs[handle - 1];
    o.nameOff = mNameUsed;
    o.nameLen = (uint16_t)len;
    memcpy(&mBuf->names[mNameUsed], name, len);
    mBuf->names[mNameUsed + len] = '\0';
    mNameUsed += len + 1;
    return true;
}

bool MtpResponder::pathOf(uint32_t handle, char *out, size_t cap) const
{
    uint32_t chain[32];
    size_t depth = 0;
    for (uint32_t h = handle; h != 0; h = mBuf->objs[h - 1].parent) {
        if ((object(h) == nullptr) || (depth >= 32)) {
            return false;
        }
        chain[depth++] = h;
    }

    size_t pos = strlen(USER_ROOT_DIR);
    if (pos + 2 > cap) {
        return false;
    }
    memcpy(out, USER_ROOT_DIR, pos);
    if (depth == 0) {
        out[pos++] = '/';
    }
    while (depth > 0) {
        const Obj &o = mBuf->objs[chain[--depth] - 1];
        if (pos + 1 + o.nameLen + 1 > cap) {
            return false;
        }
        out[pos++] = '/';
        memcpy(out + pos, nameOf(o), o.nameLen);
        pos += o.nameLen;
    }
    out[pos] = '\0';
    return true;
}

bool MtpResponder::childPath(uint32_t parent, const char *name, char *out,
                             size_t cap) const
{
    if (!pathOf(parent, out, cap)) {
        return false;
    }
    size_t pos = strlen(out);
    const size_t len = strlen(name);
    const bool slash = (out[pos - 1] != '/');
    if (pos + (slash ? 1 : 0) + len + 1 > cap) {
        return false;
    }
    if (slash) {
        out[pos++] = '/';
    }
    memcpy(out + pos, name, len + 1);
    return true;
}

/* A parent handle from the host: 0 and 0xFFFFFFFF both mean the root. */
bool MtpResponder::resolveParent(uint32_t param, uint32_t &parent) const
{
    if ((param == 0) || (param == kAll)) {
        parent = 0;
        return true;
    }
    parent = param;
    return isDir(param);
}

uint32_t MtpResponder::listChildren(uint32_t parent, uint16_t format)
{
    mListCount = 0;
    if (!pathOf(parent, mBuf->path, sizeof(mBuf->path))) {
        return kAll;
    }
    if (f_opendir(&mBuf->dir, mBuf->path) != FR_OK) {
        return kAll;
    }
    bool full = false;
    while ((f_readdir(&mBuf->dir, &mBuf->fno) == FR_OK) && (mBuf->fno.fname[0] != '\0')) {
        const char *n = mBuf->fno.fname;
        if ((strcmp(n, ".") == 0) || (strcmp(n, "..") == 0)) {
            continue;
        }
        const bool dir = (mBuf->fno.fattrib & AM_DIR) != 0;
        if ((format != 0) && (format != (dir ? FMT_ASSOCIATION : FMT_UNDEFINED))) {
            continue;
        }
        if (mListCount >= kMaxList) {
            full = true;
            break;
        }
        const uint32_t h = addObject(parent, n, strlen(n), dir);
        if (h == 0) {
            full = true;
            break;
        }
        Info &info = mBuf->listInfo[mListCount];
        info.size  = dir ? 0 : mBuf->fno.fsize;
        info.fdate = mBuf->fno.fdate;
        info.ftime = mBuf->fno.ftime;
        info.ok    = true;
        mBuf->objs[h - 1].stamp = dir ? 0 : stampOf(info.size, info.fdate, info.ftime);
        mBuf->list[mListCount++] = h;
    }
    f_closedir(&mBuf->dir);
    // From now on the host keeps this folder's contents; pollFolder() tells
    // it what changes.
    markListed(parent);
    if (full) {
        LOG_WARNING("listing of %s cut at %u entries\n", mBuf->path, (unsigned)mListCount);
    }
    return mListCount;
}

MtpResponder::Info MtpResponder::infoOf(uint32_t handle)
{
    Info info{};
    if (pathOf(handle, mBuf->path, sizeof(mBuf->path)) && (f_stat(mBuf->path, &mBuf->fno) == FR_OK)) {
        const bool dir = (mBuf->fno.fattrib & AM_DIR) != 0;
        info.size  = dir ? 0 : mBuf->fno.fsize;
        info.fdate = mBuf->fno.fdate;
        info.ftime = mBuf->fno.ftime;
        info.ok    = true;
        if ((handle != 0) && !dir) {
            mBuf->objs[handle - 1].stamp = stampOf(info.size, info.fdate, info.ftime);
        }
    }
    return info;
}

/* ==========================================================================
 * Changes made on the watch
 *
 * The host caches what it was told about each file. Anything on the watch
 * can write to the volume (logs, settings, apps, Bluetooth transfers), so
 * rather than hook every writer, the folders the host has listed are read
 * again between commands and compared with what it was told (Obj::stamp).
 * ========================================================================== */

bool MtpResponder::pollWanted() const
{
    return (mSession != 0) && mAnyListed && !mSuspended;
}

void MtpResponder::pollChanges()
{
    // An event the host has not collected yet: wait for it to catch up
    // rather than queue more behind it.
    if (mEvtBusy || !mOpLock.lock(0)) {
        return;
    }
    mStats.polls = mStats.polls + 1;
    mEvtFailed = false;
    if (mRootListed) {
        pollFolder(0);
    }
    // Folders added by this poll are not listed yet, so mObjCount growing
    // under the loop is harmless.
    for (uint32_t i = 0; (i < mObjCount) && !mEvtFailed; i++) {
        const uint8_t f = mBuf->objs[i].flags;
        if ((f & (kObjDir | kObjListed | kObjGone)) == (kObjDir | kObjListed)) {
            pollFolder(i + 1);
        }
    }
    mOpLock.release();
}

void MtpResponder::pollFolder(uint32_t folder)
{
    if (!pathOf(folder, mBuf->path, sizeof(mBuf->path)) || (f_opendir(&mBuf->dir, mBuf->path) != FR_OK)) {
        // Gone: the poll of its parent reports that.
        return;
    }

    FRESULT res;
    while (((res = f_readdir(&mBuf->dir, &mBuf->fno)) == FR_OK) && (mBuf->fno.fname[0] != '\0') &&
           !mEvtFailed) {
        const char *n = mBuf->fno.fname;
        if ((strcmp(n, ".") == 0) || (strcmp(n, "..") == 0)) {
            continue;
        }
        const bool dir = (mBuf->fno.fattrib & AM_DIR) != 0;
        const size_t len = strlen(n);
        const uint32_t stamp = dir ? 0 : stampOf(mBuf->fno.fsize, mBuf->fno.fdate, mBuf->fno.ftime);

        uint32_t h = findObject(folder, n, len);
        if (h != 0) {
            Obj &o = mBuf->objs[h - 1];
            o.flags |= kObjSeen;
            // A file the host is about to fill (SendObjectInfo, then
            // SendObject) is its own; it asks again once that is done.
            if (dir || (h == mPendingHandle)) {
                continue;
            }
            if (o.stamp == 0) {
                o.stamp = stamp;            // Never described to the host
            } else if ((o.stamp != stamp) && sendEvent(EV_OBJECT_INFO_CHANGED, h)) {
                o.stamp = stamp;
            }
            continue;
        }

        h = addObject(folder, n, len, dir);
        if (h == 0) {
            continue;                       // Table full: the host never sees it
        }
        Obj &o = mBuf->objs[h - 1];
        o.flags |= kObjSeen;
        o.stamp  = stamp;
        if (!sendEvent(EV_OBJECT_ADDED, h)) {
            o.flags |= kObjGone;            // Announced again by a later poll
        }
    }
    f_closedir(&mBuf->dir);

    // Only a complete listing says what is gone.
    const bool complete = (res == FR_OK) && !mEvtFailed;
    for (uint32_t i = 0; i < mObjCount; i++) {
        Obj &o = mBuf->objs[i];
        if ((o.parent != folder) || ((o.flags & kObjGone) != 0)) {
            continue;
        }
        if ((o.flags & kObjSeen) != 0) {
            o.flags &= (uint8_t)~kObjSeen;
        } else if (complete && ((i + 1) != mPendingHandle) &&
                   sendEvent(EV_OBJECT_REMOVED, i + 1)) {
            forgetTree(i + 1);
        }
    }
}

bool MtpResponder::sendEvent(uint16_t code, uint32_t param)
{
    if (mEvtFailed) {
        return false;
    }
    const uint32_t start = osKernelGetTickCount();
    const uint32_t limit = pdMS_TO_TICKS(kEvtWaitMs);
    while (mEvtBusy) {
        const uint32_t waited = osKernelGetTickCount() - start;
        if ((waited >= limit) || (mCfgGen != mOpGen)) {
            mEvtFailed = true;
            mStats.eventFails = mStats.eventFails + 1;
            return false;
        }
        // Only kFlagEvt is cleared; a command arriving meanwhile keeps its
        // flag for rxCommand().
        (void)osThreadFlagsWait(kFlagEvt, osFlagsWaitAny, limit - waited);
    }

    put32(mEvt + 0, sizeof(mEvt));
    put16(mEvt + 4, CT_EVENT);
    put16(mEvt + 6, code);
    put32(mEvt + 8, kAll);                  // Not tied to a transaction
    put32(mEvt + 12, param);
    mEvtBusy = true;
    if (!USBD_MTP_SendEvent(mEvt, sizeof(mEvt))) {
        mEvtBusy   = false;
        mEvtFailed = true;
        mStats.eventFails = mStats.eventFails + 1;
        return false;
    }
    mStats.events = mStats.events + 1;
    return true;
}

void MtpResponder::forgetTree(uint32_t handle)
{
    for (uint32_t i = 0; i < mObjCount; i++) {
        uint32_t depth = 0;
        for (uint32_t h = i + 1; (h != 0) && (depth < 32); h = mBuf->objs[h - 1].parent, depth++) {
            if (h == handle) {
                mBuf->objs[i].flags |= kObjGone;
                break;
            }
        }
    }
}

/* Delete a folder and everything in it, one entry at a time, with a single
   directory open: no recursion, so the stack stays flat. */
FRESULT MtpResponder::deleteTree(char *path)
{
    const size_t baseLen = strlen(path);
    for (;;) {
        FRESULT res = f_opendir(&mBuf->dir, path);
        if (res != FR_OK) {
            return res;
        }
        bool found = false;
        bool dir   = false;
        while (((res = f_readdir(&mBuf->dir, &mBuf->fno)) == FR_OK) && (mBuf->fno.fname[0] != '\0')) {
            if ((strcmp(mBuf->fno.fname, ".") != 0) && (strcmp(mBuf->fno.fname, "..") != 0)) {
                found = true;
                dir   = (mBuf->fno.fattrib & AM_DIR) != 0;
                break;
            }
        }
        f_closedir(&mBuf->dir);
        if (res != FR_OK) {
            return res;
        }

        size_t len = strlen(path);
        if (found) {
            const size_t n = strlen(mBuf->fno.fname);
            if (len + 1 + n + 1 > kPathMax) {
                return FR_INVALID_NAME;
            }
            path[len] = '/';
            memcpy(path + len + 1, mBuf->fno.fname, n + 1);
            if (dir) {
                continue;               // Empty it first
            }
            res = f_unlink(path);
            path[len] = '\0';
            if (res != FR_OK) {
                return res;
            }
            continue;
        }

        // This folder is empty now.
        res = f_unlink(path);
        if ((res != FR_OK) || (len == baseLen)) {
            return res;
        }
        while ((len > baseLen) && (path[len] != '/')) {
            len--;
        }
        path[len] = '\0';
    }
}

bool MtpResponder::hasSession(bool &ok)
{
    ok = (mSession != 0);
    if (!ok) {
        respond(RC_SESSION_NOT_OPEN);
    }
    return ok;
}

template <typename W>
void MtpResponder::emitProp(W &w, uint16_t prop, uint32_t handle, const Obj &o,
                            const Info &info)
{
    const bool dir = (o.flags & kObjDir) != 0;
    switch (prop) {
    case PROP_STORAGE_ID:        w.u32(kStorageId); break;
    case PROP_OBJECT_FORMAT:     w.u16(dir ? FMT_ASSOCIATION : FMT_UNDEFINED); break;
    case PROP_PROTECTION_STATUS: w.u16(0); break;
    case PROP_OBJECT_SIZE:       w.u64(dir ? 0 : info.size); break;
    case PROP_OBJECT_FILE_NAME:
    case PROP_NAME:              w.str(nameOf(o), o.nameLen); break;
    case PROP_DATE_CREATED:
    case PROP_DATE_MODIFIED:     w.date(info); break;
    case PROP_PARENT_OBJECT:     w.u32(o.parent); break;
    case PROP_PUOID:             w.u64(handle); w.u64(0); break;
    default:                     break;
    }
}

/* ==========================================================================
 * Operations
 * ========================================================================== */

void MtpResponder::dispatch()
{
    bool ok = true;

    if (mSuspended) {
        // Mass Storage is taking the volume. Commands with a data phase are
        // not answered early: the host would still be sending.
        if ((mCmd.code == OP_SEND_OBJECT_INFO) || (mCmd.code == OP_SEND_OBJECT) ||
                (mCmd.code == OP_SET_OBJECT_PROP_VALUE)) {
            bool complete;
            (void)rxData([](const uint8_t *, uint32_t) { return true; }, complete);
        }
        respond(RC_DEVICE_BUSY);
        return;
    }

    switch (mCmd.code) {
    case OP_GET_DEVICE_INFO:            opGetDeviceInfo(); break;
    case OP_OPEN_SESSION:               opOpenSession(); break;
    case OP_CLOSE_SESSION:
        if (hasSession(ok)) {
            mSession = 0;
            respond(RC_OK);
        }
        break;
    case OP_GET_STORAGE_IDS:            if (hasSession(ok)) { opGetStorageIds(); } break;
    case OP_GET_STORAGE_INFO:           if (hasSession(ok)) { opGetStorageInfo(); } break;
    case OP_GET_NUM_OBJECTS:            if (hasSession(ok)) { opGetNumObjects(); } break;
    case OP_GET_OBJECT_HANDLES:         if (hasSession(ok)) { opGetObjectHandles(); } break;
    case OP_GET_OBJECT_INFO:            if (hasSession(ok)) { opGetObjectInfo(); } break;
    case OP_GET_OBJECT:                 if (hasSession(ok)) { opGetObject(false); } break;
    case OP_GET_PARTIAL_OBJECT:         if (hasSession(ok)) { opGetObject(true); } break;
    case OP_DELETE_OBJECT:              if (hasSession(ok)) { opDeleteObject(); } break;
    case OP_SEND_OBJECT_INFO:           opSendObjectInfo(); break;
    case OP_SEND_OBJECT:                opSendObject(); break;
    case OP_MOVE_OBJECT:                if (hasSession(ok)) { opMoveObject(); } break;
    case OP_GET_DEVICE_PROP_DESC:       opGetDevicePropDesc(false); break;
    case OP_GET_DEVICE_PROP_VALUE:      opGetDevicePropDesc(true); break;
    case OP_GET_OBJECT_PROPS_SUPPORTED: opGetObjectPropsSupported(); break;
    case OP_GET_OBJECT_PROP_DESC:       opGetObjectPropDesc(); break;
    case OP_GET_OBJECT_PROP_VALUE:      if (hasSession(ok)) { opGetObjectPropValue(); } break;
    case OP_SET_OBJECT_PROP_VALUE:      opSetObjectPropValue(); break;
    case OP_GET_OBJECT_PROP_LIST:       if (hasSession(ok)) { opGetObjectPropList(); } break;
    case OP_GET_OBJECT_REFERENCES:      if (hasSession(ok)) { opGetObjectReferences(); } break;
    default:
        LOG_DEBUG("unsupported op 0x%04X\n", mCmd.code);
        respond(RC_OPERATION_NOT_SUPPORTED);
        break;
    }
}

void MtpResponder::opGetDeviceInfo()
{
    char serial[33];
    makeSerial(serial);

    sendData([&](Writer &w) {
        w.u16(100);                         // Standard version 1.00
        w.u32(6);                           // Vendor extension: Microsoft (MTP)
        w.u16(100);
        w.str("microsoft.com: 1.0;");
        w.u16(0);                           // Functional mode
        w.array16(kOperations, COUNT(kOperations));
        w.array16(kEvents, COUNT(kEvents)); // Events supported
        w.array16(kDeviceProps, COUNT(kDeviceProps));
        w.u32(0);                           // Capture formats
        w.array16(kFormats, COUNT(kFormats));
        w.str("UNA");
        w.str(PROJECT_NAME);
        w.str(BUILD_VERSION);
        w.str(serial);
    });
    respond(RC_OK);
}

void MtpResponder::opOpenSession()
{
    if (mCmd.p[0] == 0) {
        respond(RC_INVALID_PARAMETER);
        return;
    }
    if (mSession != 0) {
        respond(RC_SESSION_ALREADY_OPEN, 1, mSession);
        return;
    }
    mSession       = mCmd.p[0];
    mPendingHandle = 0;
    // Handles are only good for one session; start afresh, as the volume
    // may have changed under Mass Storage since the last one.
    resetObjects();
    LOG_INFO("session %u open\n", (unsigned)mSession);
    respond(RC_OK);
}

void MtpResponder::opGetStorageIds()
{
    sendData([&](Writer &w) {
        w.u32(1);
        w.u32(kStorageId);
    });
    respond(RC_OK);
}

void MtpResponder::opGetStorageInfo()
{
    if (mCmd.p[0] != kStorageId) {
        respond(RC_INVALID_STORAGE_ID);
        return;
    }

    uint64_t capacity = 0;
    uint64_t freeBytes = 0;
    DWORD freeClusters = 0;
    FATFS *fs = nullptr;
    if (f_getfree(USER_ROOT_DIR, &freeClusters, &fs) == FR_OK) {
        const uint64_t cluster = (uint64_t)fs->csize * FF_MAX_SS;
        capacity  = (uint64_t)(fs->n_fatent - 2) * cluster;
        freeBytes = (uint64_t)freeClusters * cluster;
    }

    sendData([&](Writer &w) {
        w.u16(0x0003);                      // Fixed RAM
        w.u16(0x0002);                      // Generic hierarchical
        w.u16(0x0000);                      // Read-write
        w.u64(capacity);
        w.u64(freeBytes);
        w.u32(kAll);                        // Free space in objects: n/a
        w.str("Watch");
        w.str("");
    });
    respond(RC_OK);
}

void MtpResponder::opGetNumObjects()
{
    uint32_t parent;
    if ((mCmd.p[0] != kAll) && (mCmd.p[0] != kStorageId)) {
        respond(RC_INVALID_STORAGE_ID);
        return;
    }
    if (!resolveParent(mCmd.p[2], parent)) {
        respond(RC_INVALID_PARENT_OBJECT);
        return;
    }
    const uint32_t n = listChildren(parent, (uint16_t)mCmd.p[1]);
    if (n == kAll) {
        respond(RC_INVALID_PARENT_OBJECT);
        return;
    }
    respond(RC_OK, 1, n);
}

void MtpResponder::opGetObjectHandles()
{
    uint32_t parent;
    if ((mCmd.p[0] != kAll) && (mCmd.p[0] != kStorageId)) {
        respond(RC_INVALID_STORAGE_ID);
        return;
    }
    // Parent 0 asks for every object on the store; this lists the root
    // only, as many small devices do. Hosts browse folder by folder.
    if (!resolveParent(mCmd.p[2], parent)) {
        respond(RC_INVALID_PARENT_OBJECT);
        return;
    }
    const uint32_t n = listChildren(parent, (uint16_t)mCmd.p[1]);
    if (n == kAll) {
        respond(RC_INVALID_PARENT_OBJECT);
        return;
    }
    sendData([&](Writer &w) {
        w.u32(n);
        for (uint32_t i = 0; i < n; i++) {
            w.u32(mBuf->list[i]);
        }
    });
    respond(RC_OK);
}

void MtpResponder::opGetObjectInfo()
{
    const uint32_t h = mCmd.p[0];
    const Obj *o = object(h);
    if (o == nullptr) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    const Info info = infoOf(h);
    if (!info.ok) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    const bool dir = (o->flags & kObjDir) != 0;

    sendData([&](Writer &w) {
        w.u32(kStorageId);
        w.u16(dir ? FMT_ASSOCIATION : FMT_UNDEFINED);
        w.u16(0);                                       // Protection
        w.u32((info.size > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)info.size);
        w.u16(0);                                       // Thumb format
        w.u32(0); w.u32(0); w.u32(0);                   // Thumb size, w, h
        w.u32(0); w.u32(0); w.u32(0);                   // Image w, h, depth
        w.u32(o->parent);
        w.u16(dir ? ASSOC_GENERIC_FOLDER : 0);
        w.u32(0);                                       // Association desc
        w.u32(0);                                       // Sequence number
        w.str(nameOf(*o), o->nameLen);
        w.date(info);                                   // Date created
        w.date(info);                                   // Date modified
        w.str("");                                      // Keywords
    });
    respond(RC_OK);
}

void MtpResponder::opGetObject(bool partial)
{
    const uint32_t h = mCmd.p[0];
    const Obj *o = object(h);
    if ((o == nullptr) || ((o->flags & kObjDir) != 0)) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    if (!pathOf(h, mBuf->path, sizeof(mBuf->path))) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    FRESULT res = f_open(&mBuf->fil, mBuf->path, FA_READ);
    if (res != FR_OK) {
        respond(mapResult(res));
        return;
    }

    uint64_t size = f_size(&mBuf->fil);
    uint32_t offset = 0;
    if (partial) {
        offset = mCmd.p[1];
        if (offset > size) {
            offset = (uint32_t)size;
        }
        size -= offset;
        if (size > mCmd.p[2]) {
            size = mCmd.p[2];
        }
        if ((offset != 0) && (f_lseek(&mBuf->fil, offset) != FR_OK)) {
            f_close(&mBuf->fil);
            respond(RC_GENERAL_ERROR);
            return;
        }
    }
    if (size > 0xFFFFFFFFu - kHeader) {
        f_close(&mBuf->fil);
        respond(RC_GENERAL_ERROR);
        return;
    }

    // The file is read straight into the transmit buffers. If the watch
    // shortens it meanwhile, the rest is sent as zeros: the length in the
    // header is already promised.
    const uint32_t total = kHeader + (uint32_t)size;
    uint8_t *b = txBuf();
    put32(b + 0, total);
    put16(b + 4, CT_DATA);
    put16(b + 6, mCmd.code);
    put32(b + 8, mCmd.tid);
    uint32_t pos = kHeader;
    uint32_t remaining = (uint32_t)size;
    bool ok = true;

    while (ok && (remaining > 0)) {
        uint32_t n = kBufSize - pos;
        if (n > remaining) {
            n = remaining;
        }
        UINT br = 0;
        if ((f_read(&mBuf->fil, b + pos, n, &br) != FR_OK) || (br < n)) {
            memset(b + pos + br, 0, n - br);
        }
        pos += n;
        remaining -= n;
        if ((pos == kBufSize) || (remaining == 0)) {
            ok  = txFlush(pos);
            b   = txBuf();
            pos = 0;
        }
    }
    if (ok && (pos > 0)) {
        ok = txFlush(pos);          // Header only: an empty file
    }
    f_close(&mBuf->fil);
    if (ok && ((total % mps()) == 0)) {
        ok = txZlp();
    }
    if (ok) {
        if (partial) {
            respond(RC_OK, 1, (uint32_t)size);
        } else {
            respond(RC_OK);
        }
    }
}

void MtpResponder::opDeleteObject()
{
    const uint32_t h = mCmd.p[0];
    if (h == kAll) {
        respond(RC_ACCESS_DENIED);      // "Delete everything": not over MTP
        return;
    }
    const Obj *o = object(h);
    if ((o == nullptr) || !pathOf(h, mBuf->path, sizeof(mBuf->path))) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    LOG_INFO("delete %s\n", mBuf->path);
    const FRESULT res = ((o->flags & kObjDir) != 0) ? deleteTree(mBuf->path) : f_unlink(mBuf->path);
    if (res == FR_OK) {
        forgetTree(h);
        respond(RC_OK);
    } else {
        respond(((o->flags & kObjDir) != 0) ? RC_PARTIAL_DELETION : mapResult(res));
    }
}

void MtpResponder::opSendObjectInfo()
{
    uint32_t len = 0;
    const bool got = rxDataset(len);
    if (mAbort) {
        return;
    }
    bool ok;
    if (!hasSession(ok)) {
        return;
    }
    if (!got) {
        respond(RC_INVALID_DATASET);
        return;
    }
    if ((mCmd.p[0] != 0) && (mCmd.p[0] != kStorageId)) {
        respond(RC_INVALID_STORAGE_ID);
        return;
    }
    uint32_t parent;
    if (!resolveParent(mCmd.p[1], parent)) {
        respond(RC_INVALID_PARENT_OBJECT);
        return;
    }

    Reader r(mBuf->tx[0], len);
    (void)r.u32();                              // Storage
    const uint16_t format = r.u16();
    (void)r.u16();                              // Protection
    const uint32_t size = r.u32();
    (void)r.u16();                              // Thumb format
    for (int i = 0; i < 6; i++) {
        (void)r.u32();                          // Thumb size .. image depth
    }
    (void)r.u32();                              // Parent (the parameter wins)
    (void)r.u16();                              // Association type
    (void)r.u32();                              // Association desc
    (void)r.u32();                              // Sequence number
    size_t nameLen = 0;
    if (!r.str(mBuf->name, sizeof(mBuf->name), nameLen) || (nameLen == 0) ||
            (strchr(mBuf->name, '/') != nullptr) || (strchr(mBuf->name, '\\') != nullptr) ||
            (strchr(mBuf->name, ':') != nullptr)) {
        respond(RC_INVALID_DATASET);
        return;
    }
    if (!childPath(parent, mBuf->name, mBuf->path, sizeof(mBuf->path))) {
        respond(RC_INVALID_PARENT_OBJECT);
        return;
    }

    const bool dir = (format == FMT_ASSOCIATION);
    FRESULT res;
    if (dir) {
        res = f_mkdir(mBuf->path);
        if (res == FR_EXIST) {
            res = FR_OK;
        }
    } else {
        // Created now, so the handle names a real file; SendObject fills it.
        res = f_open(&mBuf->fil, mBuf->path, FA_WRITE | FA_CREATE_ALWAYS);
        if (res == FR_OK) {
            f_close(&mBuf->fil);
        }
    }
    if (res != FR_OK) {
        respond(mapResult(res));
        return;
    }

    const uint32_t h = addObject(parent, mBuf->name, nameLen, dir);
    if (h == 0) {
        respond(RC_STORE_FULL);
        return;
    }
    LOG_INFO("%s %s (%u bytes)\n", dir ? "mkdir" : "create", mBuf->path, (unsigned)size);
    mPendingHandle = dir ? 0 : h;
    mPendingSize   = size;
    respond(RC_OK, 3, kStorageId, parent, h);
}

void MtpResponder::opSendObject()
{
    const uint32_t h = mPendingHandle;
    mPendingHandle = 0;

    FRESULT res = FR_INVALID_OBJECT;
    if ((mSession != 0) && (h != 0) && pathOf(h, mBuf->path, sizeof(mBuf->path))) {
        res = f_open(&mBuf->fil, mBuf->path, FA_WRITE | FA_CREATE_ALWAYS);
    }
    const bool open = (res == FR_OK);
    bool full = false;

    bool complete = false;
    const bool ok = rxData([&](const uint8_t *p, uint32_t n) {
        if (!open) {
            return false;
        }
        UINT bw = 0;
        res = f_write(&mBuf->fil, p, n, &bw);
        if ((res == FR_OK) && (bw < n)) {
            full = true;
        }
        return (res == FR_OK) && !full;
    }, complete);

    if (open) {
        const FRESULT closeRes = f_close(&mBuf->fil);
        if (res == FR_OK) {
            res = closeRes;
        }
        // A cancelled or failed upload leaves no partial file behind: the
        // host forgets the object, so nothing would ever remove it.
        const bool stored = ok && !mAbort && complete && !full && (res == FR_OK);
        if (!stored) {
            (void)f_unlink(mBuf->path);
            mBuf->objs[h - 1].flags |= kObjGone;
            LOG_INFO("upload of %s not completed, removed\n", mBuf->path);
        }
    }
    if (!ok || mAbort) {
        return;
    }

    if (mSession == 0) {
        respond(RC_SESSION_NOT_OPEN);
    } else if (h == 0) {
        respond(RC_NO_VALID_OBJECT_INFO);
    } else if (full) {
        respond(RC_STORE_FULL);
    } else if (res != FR_OK) {
        respond(mapResult(res));
    } else if (!complete) {
        respond(RC_INCOMPLETE_TRANSFER);
    } else {
        respond(RC_OK);
    }
}

void MtpResponder::opMoveObject()
{
    const uint32_t h = mCmd.p[0];
    const Obj *o = object(h);
    uint32_t parent;
    if (o == nullptr) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    if ((mCmd.p[1] != 0) && (mCmd.p[1] != kStorageId)) {
        respond(RC_INVALID_STORAGE_ID);
        return;
    }
    if (!resolveParent(mCmd.p[2], parent)) {
        respond(RC_INVALID_PARENT_OBJECT);
        return;
    }
    // Not into itself or below it.
    for (uint32_t p = parent; p != 0; p = mBuf->objs[p - 1].parent) {
        if (p == h) {
            respond(RC_INVALID_PARENT_OBJECT);
            return;
        }
    }
    if (!pathOf(h, mBuf->path, sizeof(mBuf->path)) ||
            !childPath(parent, nameOf(*o), mBuf->path2, sizeof(mBuf->path2))) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    const FRESULT res = f_rename(mBuf->path, mBuf->path2);
    if (res == FR_OK) {
        mBuf->objs[h - 1].parent = parent;
    }
    respond(mapResult(res));
}

void MtpResponder::opGetDevicePropDesc(bool valueOnly)
{
    if (mCmd.p[0] != DPROP_FRIENDLY_NAME) {
        respond(RC_DEVICE_PROP_NOT_SUPPORTED);
        return;
    }
    sendData([&](Writer &w) {
        if (!valueOnly) {
            w.u16(DPROP_FRIENDLY_NAME);
            w.u16(DT_STR);
            w.u8(0);                    // Get only
            w.str(PROJECT_NAME);        // Factory default
        }
        w.str(PROJECT_NAME);            // Current value
        if (!valueOnly) {
            w.u8(0);                    // No form
        }
    });
    respond(RC_OK);
}

void MtpResponder::opGetObjectPropsSupported()
{
    sendData([&](Writer &w) {
        w.array16(kObjectProps, COUNT(kObjectProps));
    });
    respond(RC_OK);
}

void MtpResponder::opGetObjectPropDesc()
{
    const uint16_t prop = (uint16_t)mCmd.p[0];
    const uint16_t type = propType(mCmd.p[0]);
    if (type == 0) {
        respond(RC_INVALID_OBJECT_PROP_CODE);
        return;
    }
    sendData([&](Writer &w) {
        w.u16(prop);
        w.u16(type);
        w.u8((prop == PROP_OBJECT_FILE_NAME) ? 1 : 0);  // Only renames are settable
        switch (type) {                                 // Default value
        case DT_UINT16:  w.u16(0); break;
        case DT_UINT32:  w.u32(0); break;
        case DT_UINT64:  w.u64(0); break;
        case DT_UINT128: w.u64(0); w.u64(0); break;
        default:         w.u8(0); break;                // Empty string
        }
        w.u32(0);                                       // Group code
        w.u8(0);                                        // No form
    });
    respond(RC_OK);
}

void MtpResponder::opGetObjectPropValue()
{
    const uint32_t h = mCmd.p[0];
    const uint16_t prop = (uint16_t)mCmd.p[1];
    const Obj *o = object(h);
    if (o == nullptr) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    if (propType(mCmd.p[1]) == 0) {
        respond(RC_INVALID_OBJECT_PROP_CODE);
        return;
    }
    const Info info = infoOf(h);
    sendData([&](Writer &w) {
        emitProp(w, prop, h, *o, info);
    });
    respond(RC_OK);
}

void MtpResponder::opSetObjectPropValue()
{
    uint32_t len = 0;
    const bool got = rxDataset(len);
    if (mAbort) {
        return;
    }
    bool ok;
    if (!hasSession(ok)) {
        return;
    }
    const uint32_t h = mCmd.p[0];
    const Obj *o = object(h);
    if (o == nullptr) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    if (mCmd.p[1] != PROP_OBJECT_FILE_NAME) {
        respond((propType(mCmd.p[1]) == 0) ? RC_INVALID_OBJECT_PROP_CODE : RC_ACCESS_DENIED);
        return;
    }
    Reader r(mBuf->tx[0], len);
    size_t nameLen = 0;
    if (!got || !r.str(mBuf->name, sizeof(mBuf->name), nameLen) || (nameLen == 0) ||
            (strchr(mBuf->name, '/') != nullptr) || (strchr(mBuf->name, '\\') != nullptr) ||
            (strchr(mBuf->name, ':') != nullptr)) {
        respond(RC_INVALID_DATASET);
        return;
    }
    if (!pathOf(h, mBuf->path, sizeof(mBuf->path)) ||
            !childPath(o->parent, mBuf->name, mBuf->path2, sizeof(mBuf->path2))) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    LOG_INFO("rename %s -> %s\n", mBuf->path, mBuf->name);
    const FRESULT res = f_rename(mBuf->path, mBuf->path2);
    if (res == FR_OK) {
        if (!setName(h, mBuf->name, nameLen)) {
            mBuf->objs[h - 1].flags |= kObjGone;     // Out of name space: forget it
        }
    }
    respond(mapResult(res));
}

void MtpResponder::opGetObjectPropList()
{
    const uint32_t handle = mCmd.p[0];
    const uint16_t format = (uint16_t)mCmd.p[1];
    const uint32_t prop   = mCmd.p[2];
    const uint32_t group  = mCmd.p[3];
    const uint32_t depth  = mCmd.p[4];

    if (prop == 0) {
        respond(RC_GROUP_UNSUPPORTED);
        return;
    }
    if ((prop != kAll) && (propType(prop) == 0)) {
        respond(RC_OBJECT_PROP_NOT_SUPPORTED);
        return;
    }
    (void)group;

    if (depth == 0) {
        if (handle == 0) {
            sendData([&](Writer &w) { w.u32(0); });
            respond(RC_OK);
            return;
        }
        const Obj *o = object(handle);
        if (o == nullptr) {
            respond((handle == kAll) ? RC_DEPTH_UNSUPPORTED : RC_INVALID_OBJECT_HANDLE);
            return;
        }
        mBuf->list[0] = handle;
        mBuf->listInfo[0] = infoOf(handle);
        mListCount = 1;
    } else if (depth == 1) {
        uint32_t parent;
        if (!resolveParent(handle, parent)) {
            if (object(handle) != nullptr) {
                sendData([&](Writer &w) { w.u32(0); });   // A file: no children
                respond(RC_OK);
            } else {
                respond(RC_INVALID_OBJECT_HANDLE);
            }
            return;
        }
        if (listChildren(parent, format) == kAll) {
            respond(RC_INVALID_OBJECT_HANDLE);
            return;
        }
    } else {
        respond(RC_DEPTH_UNSUPPORTED);
        return;
    }

    const uint32_t nprops = (prop == kAll) ? COUNT(kObjectProps) : 1;
    sendData([&](Writer &w) {
        w.u32(mListCount * nprops);
        for (uint32_t i = 0; i < mListCount; i++) {
            const uint32_t h = mBuf->list[i];
            const Obj &o = mBuf->objs[h - 1];
            for (uint32_t k = 0; k < nprops; k++) {
                const uint16_t pc = (prop == kAll) ? kObjectProps[k] : (uint16_t)prop;
                w.u32(h);
                w.u16(pc);
                w.u16(propType(pc));
                emitProp(w, pc, h, o, mBuf->listInfo[i]);
            }
        }
    });
    respond(RC_OK);
}

void MtpResponder::opGetObjectReferences()
{
    if (object(mCmd.p[0]) == nullptr) {
        respond(RC_INVALID_OBJECT_HANDLE);
        return;
    }
    sendData([&](Writer &w) { w.u32(0); });
    respond(RC_OK);
}
