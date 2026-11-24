/**
 ******************************************************************************
 * @file    MtpResponder.hpp
 * @brief   MTP responder: serves the user volume (USER_ROOT_DIR) to a
 *          computer over USB while the watch keeps using it.
 *
 *          Unlike Mass Storage, the computer never sees disk blocks. It asks
 *          for files ("list this folder", "read this file", "write this
 *          file") and this task does each request with FatFs, the same way
 *          the rest of the kernel reaches the volume. FatFs is the only
 *          writer, so both sides can work at the same time.
 *
 *          The USB interrupt (usbd_mtp.c) only moves packets; everything else
 *          runs in this task.
 ******************************************************************************
 */

#ifndef __MTP_RESPONDER_HPP
#define __MTP_RESPONDER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "OS.hpp"
#include "ff.h"

class MtpResponder {
public:
    static MtpResponder& GetInstance()
    {
        static MtpResponder sInstance;
        return sInstance;
    }

    /** Start the task and hook it to the USB class. Once, at boot. */
    bool init();

    /**
     * Before Mass Storage takes the volume: wait for the operation in
     * progress, if any, and refuse new ones until resume().
     * @return false if an operation was still running after timeoutMs.
     */
    bool suspend(uint32_t timeoutMs);

    /** Mass Storage has given the volume back. */
    void resume();

    /** Counters for the `usb` CLI command, to see where an exchange stops. */
    struct Stats {
        uint32_t cfgOn, cfgOff;
        uint32_t rxArmed, rxDone, lastRxLen;
        uint32_t commands, lastOp, lastResp;
        uint32_t txStart, txDone, txFail;
        uint32_t aborts, cancels;
        uint32_t rxDropped, notCommand, lastHeader, lastFlags;
        uint32_t polls, events, eventFails;
    };
    const volatile Stats &stats() const { return mStats; }
    bool isOpen() const { return mOpen; }
    uint32_t session() const { return mSession; }

    // Everything below is .bss, which comes out of the heap apps share
    // (see the linker script), so it is kept to about 64 KB.
    static constexpr uint32_t kBufSize    = 4096;   // Multiple of the HS packet size
    static constexpr uint32_t kMaxObjects = 1024;   // Handles given out per session
    static constexpr uint32_t kNamePool   = 16 * 1024;
    static constexpr uint32_t kMaxList    = 512;    // Entries listed per folder
    static constexpr uint32_t kPathMax    = 512;
    static constexpr uint32_t kStorageId  = 0x00010001;

private:
    MtpResponder() = default;

    class Writer;
    struct Reader;

    struct Obj {
        uint32_t parent;    // 0 = root of the volume
        uint32_t nameOff;
        uint16_t nameLen;
        uint8_t  flags;
        uint8_t  reserved;
        uint32_t stamp;     // Size and date the host was last told; 0 = not yet
    };

    struct Info {
        uint64_t size;
        uint16_t fdate;
        uint16_t ftime;
        bool     ok;
    };

    struct Command {
        uint16_t code;
        uint32_t tid;
        uint32_t nparams;
        uint32_t p[5];
    };

    void thread();
    void dispatch();

    // Operations
    void opGetDeviceInfo();
    void opOpenSession();
    void opGetStorageIds();
    void opGetStorageInfo();
    void opGetNumObjects();
    void opGetObjectHandles();
    void opGetObjectInfo();
    void opGetObject(bool partial);
    void opDeleteObject();
    void opSendObjectInfo();
    void opSendObject();
    void opMoveObject();
    void opGetDevicePropDesc(bool valueOnly);
    void opGetObjectPropsSupported();
    void opGetObjectPropDesc();
    void opGetObjectPropValue();
    void opSetObjectPropValue();
    void opGetObjectPropList();
    void opGetObjectReferences();

    // Transport
    bool rxCommand(uint32_t &len);
    uint32_t waitEvents(uint32_t want, uint32_t timeoutMs);
    bool rxPacket(uint32_t want, uint32_t &len, uint32_t timeoutMs);
    bool txSend(const uint8_t *buf, uint32_t len);
    bool txIdle();
    uint8_t *txBuf() { return mTx[mTxCur]; }
    bool txFlush(uint32_t len);
    bool txZlp();
    void respond(uint16_t code, uint32_t nparams = 0, uint32_t p0 = 0,
                 uint32_t p1 = 0, uint32_t p2 = 0);
    template <typename Gen> bool sendData(Gen &&gen);
    template <typename Sink> bool rxData(Sink &&sink, bool &complete);
    bool rxDataset(uint32_t &len);
    void abortTransfers();
    uint16_t mps() const;

    // Changes made on the watch, reported to the host as events
    bool pollWanted() const;
    void pollChanges();
    void pollFolder(uint32_t folder);
    bool sendEvent(uint16_t code, uint32_t param);

    // Objects
    void resetObjects();
    uint32_t findObject(uint32_t parent, const char *name, size_t len) const;
    uint32_t addObject(uint32_t parent, const char *name, size_t len, bool dir);
    void markListed(uint32_t folder);
    const Obj *object(uint32_t handle) const;
    bool isDir(uint32_t handle) const;
    const char *nameOf(const Obj &o) const { return &mNames[o.nameOff]; }
    bool setName(uint32_t handle, const char *name, size_t len);
    bool pathOf(uint32_t handle, char *out, size_t cap) const;
    bool childPath(uint32_t parent, const char *name, char *out, size_t cap) const;
    bool resolveParent(uint32_t param, uint32_t &parent) const;
    uint32_t listChildren(uint32_t parent, uint16_t format);
    Info infoOf(uint32_t handle);
    void forgetTree(uint32_t handle);
    FRESULT deleteTree(char *path);
    bool hasSession(bool &ok);

    template <typename W>
    void emitProp(W &w, uint16_t prop, uint32_t handle, const Obj &o, const Info &info);

    static void cbConfigured(bool on);
    static void cbRxDone(uint32_t len);
    static void cbTxDone();
    static void cbEventDone();
    static void cbCancel();
    void signal(uint32_t flags);

    static constexpr size_t kStackBytes = 6 * 1024;
    OS::Thread<MtpResponder, kStackBytes> mThread;
    OS::Mutex                             mOpLock;
    osThreadId_t                          mThreadId = nullptr;

    std::atomic<bool>     mOpen{false};
    std::atomic<bool>     mSuspended{false};
    volatile bool         mRxArmed = false;
    volatile bool         mTxBusy  = false;
    volatile uint32_t     mRxLen   = 0;
    bool                  mAbort   = false;
    volatile Stats        mStats{};
    volatile uint32_t     mCfgGen  = 0;     // Bumped each time the interface opens or closes
    uint32_t              mOpGen   = 0;     // mCfgGen when the current operation began
    volatile bool         mEvtBusy = false; // An event is queued on the interrupt endpoint
    bool                  mEvtFailed = false;
    bool                  mRootListed = false;
    bool                  mAnyListed  = false;
    uint32_t              mLastPoll   = 0;  // Kernel ticks

    Command  mCmd{};
    uint32_t mSession = 0;
    uint32_t mPendingHandle = 0;        // From SendObjectInfo, for SendObject
    uint32_t mPendingSize = 0;

    uint32_t mObjCount = 0;
    uint32_t mNameUsed = 0;
    uint32_t mListCount = 0;
    uint8_t  mTxCur = 0;

    FIL      mFil;
    DIR      mDir;
    FILINFO  mFno;
    char     mPath[kPathMax];
    char     mPath2[kPathMax];
    char     mName[256];

    alignas(4) uint8_t mRx[kBufSize];
    alignas(4) uint8_t mTx[2][kBufSize];
    alignas(4) uint8_t mResp[32];
    alignas(4) uint8_t mEvt[16];
    Obj      mObjs[kMaxObjects];
    char     mNames[kNamePool];
    uint32_t mList[kMaxList];
    Info     mListInfo[kMaxList];
};

#endif /* __MTP_RESPONDER_HPP */
