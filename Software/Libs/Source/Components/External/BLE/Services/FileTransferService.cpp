/**
 ******************************************************************************
 * @file    FileTransferService.cpp
 * @brief   BLE File Transfer Service (FTS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.SRV.FileTransfer"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "FileTransferService.hpp"
#include "OS.hpp"
#include "Crc/Crc32.hpp"

#include <algorithm> // for std::min

namespace Ble
{

FileTransferService::FileTransferService(Interface::IBle &ble,
        Callback *pCallback, Interface::IFileSystem &fs) :
        mBle(ble), mObserver(pCallback), mFs(fs)
{

    // Create empty objects
    mFile = mFs.file("");
    mDir = mFs.dir("");

    // Create the OTS service
    mSrv = ble.addService(UUID(0xFEBB));    // Adafruit's 16-bit service UUID

    // Add Version Characteristic
    // Simple unsigned 32-bit integer version number (see skProtocolVersion).
    mChrVersion = ble.addCharacteristic(mSrv,
                UUID("ADAF0001-4669-6C65-5472-616E73666572"), sizeof(skProtocolVersion),
                CharProp::READ);

    // Add Raw Transfer Characteristic
    mChrRawTransfer = ble.addCharacteristic(mSrv,
                UUID("ADAF0002-4669-6C65-5472-616E73666572"), ble.getMaxCharacteristicLen(),
                (CharProp::WRITE_WO_RESP | CharProp::NOTIFY));
    mBle.attachCharacteristicCallback(mChrRawTransfer, this);


    mBle.attachStatusCallback(this);
}

FileTransferService::~FileTransferService()
{
    mBle.detachStatusCallback(this);

    mBle.attachCharacteristicCallback(mChrRawTransfer, nullptr);
    mBle.delCharacteristic(mChrRawTransfer);

    mBle.delCharacteristic(mChrVersion);

    mBle.delService(mSrv);
}


void FileTransferService::onEnable()
{
    mBle.setCharacteristicValue(mChrVersion,
            (const uint8_t*)&skProtocolVersion,
            sizeof(skProtocolVersion));
}

void FileTransferService::onDisconnect(uint8_t reason)
{
    if (mFile->isOpen()) {
        mFile->close();
    }
    mReadState.active = false;
    mWriteAck.pending = false;
    mWriteContext.totalSize = 0;
    mWriteContext.time = 0;
}

void FileTransferService::onTxReady()
{
    // Controller TX buffers freed (una_ble_gatt_tx_pool_available_event). Resume a
    // parked windowed read, and/or deliver a write ACK that was deferred because
    // the pool was full -- both without blocking the HCI event thread.
    if (mReadState.active && mReadState.offset < mReadState.windowEnd) {
        pumpRead();
    }
    if (mWriteAck.pending) {
        bool poolFull = false;
        if (mBle.trySetCharacteristicValue(mChrRawTransfer, mWriteAck.data,
                mWriteAck.len, &poolFull)) {
            mWriteAck.pending = false;   // delivered
        } else if (!poolFull) {
            mWriteAck.pending = false;   // hard error: drop it, don't wedge
        }
        // else still pool-full: keep parked; a later onTxReady() retries.
    }
}

void FileTransferService::onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
        bool notify, bool indicate)
{
    if (chrHandle == mChrRawTransfer) {
        mNotificationEnabled = notify;
        LOG_DEBUG("Notification %s\n",mNotificationEnabled ? "enabled" : "disabled");
    }
}

void FileTransferService::onWrite(Ble::CharacteristicHandle chrHandle,
            const uint8_t *pData, uint16_t len)
{
    if (chrHandle == mChrRawTransfer) {

        if (!mNotificationEnabled) {
            return;
        }

        LOG_DEBUG("Request %d bytes:\n", len);
        LOG_DEBUG_DUMP(pData, len);

        uint8_t command = pData[0];

        // A parked read window (mReadState) or deferred write ACK
        // (mWriteAck) belongs to a specific in-progress transfer. If the
        // client abandons that transfer without disconnecting and issues a
        // different top-level command, the leftover state is stale and must
        // not fire against the new command's mFile (a late onTxReady() could
        // otherwise pump a read against a write-mode handle, or inject a
        // WRITE_PACING into a read stream). READ_PACING/WRITE_DATA are
        // continuations of their own transfer and keep their own direction's
        // state; every other command supersedes the opposite direction.
        if (command != Command::READ && command != Command::READ_PACING) {
            mReadState.active = false;
        }
        if (command != Command::WRITE && command != Command::WRITE_DATA) {
            mWriteAck.pending = false;
        }
        // The lost-terminal-ACK re-ACK (see writeHandler) may only answer a tail
        // resent by WRITE_DATA. Any other command ends that window, so a
        // completed transfer's state can never certify a later, unrelated one
        // (a failed/lost WRITE, or an interposed READ/DELETE/MOVE). A successful
        // WRITE re-arms mWriteContext itself.
        if (command != Command::WRITE_DATA) {
            mWriteContext.complete = false;
        }

        switch (command) {
            case Command::READ:
            case Command::READ_PACING:
                readHandler(pData, len);
                break;
            case Command::WRITE:
            case Command::WRITE_DATA:
                writeHandler(pData, len);
                break;
            case Command::DELETE:
                deleteHandler(pData, len);
                break;
            case Command::MKDIR:
                makeDirHandler(pData, len);
                break;
            case Command::LISTDIR:
                listDirHandler(pData, len);
                break;
            case Command::MOVE:
                moveHandler(pData, len);
                break;
            case Command::DIGEST:
                digestHandler(pData, len);
                break;

            default:
                sendErr(command, Responce::ERROR_PROTOCOL);
                break;
        }
    }
}

const char* FileTransferService::TerminatePath(char* dst, uint16_t dstSize, const uint8_t *pPath, uint16_t pathLen)
{
    // Make '\0' - terminated path
    if (pathLen >= dstSize) {
        LOG_ERROR("Requested path is too long [%.*s]\n", pathLen, pPath);
        dst[0] = '\0';
    } else {
        snprintf(dst, dstSize, "%.*s", pathLen, (const char*)pPath);
        dst[pathLen] = '\0';
    }

    return dst;
}

void FileTransferService::readHandler(const uint8_t *pData, uint16_t len)
{
    // Test command: 15 bytes of /Update/test.ota
    // 10 00 10 00 00 00 00 00 0F 00 00 00 2F 55 70 64 61 74 65 2F 74 65 73 74 2E 6F 74 61

    uint8_t command = pData[0];
    uint32_t chunkOffset = 0;
    uint32_t chunkSize = 0;

    if (command == Command::READ) {

        ReadFileRequest req {};
        memcpy(&req, pData, sizeof(req));

        // Validate command length
        uint16_t expectedLength = sizeof(req) + req.pathLength;
        if (len != expectedLength) {
            LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
            sendErr(command, Responce::ERROR_PROTOCOL);
            return;
        }

        LOG_DEBUG("Read start: [%.*s], offset %u, size %u\n", req.pathLength, &pData[sizeof(req)], req.chunkOffset, req.chunkSize);

        if (pData[sizeof(req)] != '/') {
            LOG_WARNING("Invalid path [%.*s]\n", req.pathLength, &pData[sizeof(req)]);
            sendErr(command, Responce::ERROR_PROTOCOL);
            return;
        }

        mReadContext.sentPct = 0;

        // This will close file if it was open
        mFile->setPath(TerminatePath(mPath, sizeof(mPath), &pData[sizeof(req)], req.pathLength));

        chunkOffset = req.chunkOffset;
        chunkSize = req.chunkSize;

        if (mObserver) {
            mObserver->onFileRead(mFile->getPath(), 0);
        }

    } else if (command == Command::READ_PACING) {

        ReadFileMoreDataRequest req { };
        memcpy(&req, pData, sizeof(req));

        // Validate command length
        uint16_t expectedLength = sizeof(req);
        if (len != expectedLength) {
            LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
            sendErr(command, Responce::ERROR_PROTOCOL);
            return;
        }

        LOG_DEBUG("Read chunk [%s], offset %u, size %u\n", mFile->getPath(), req.chunkOffset, req.chunkSize);

        chunkOffset = req.chunkOffset;
        chunkSize = req.chunkSize;
    }

    if (!mFile->exist()) {
        sendErr(command, Responce::ERROR_NO_FILE);
        LOG_ERROR("File not exist [%s]\n", mFile->getPath());
        return;
    }

    size_t fileLen = mFile->size();

    // Keep the file open across the whole transfer. The read path used to
    // open()+close() on every chunk; on eMMC that is a directory walk per
    // ~200 bytes and made reads slower than writes. Open once (whenever it is
    // found closed) and close on completion. Other close paths remain: a new
    // READ (setPath closes the prior handle), the read error below, and
    // onDisconnect().
    if (!mFile->isOpen() && !mFile->open()) {
        sendErr(command, Responce::ERROR);
        LOG_ERROR("Failed to open file [%s]\n", mFile->getPath());
        return;
    }

    // Event-driven windowed read. Set up the window [offset, windowEnd) and let
    // pumpRead() emit it as a burst of self-describing READ_DATA notifications.
    // pumpRead() parks on a full TX pool and resumes from onTxReady(), so the
    // pool is kept fed without blocking the HCI event thread. A classic client
    // requests <= one packet, so the window is a single notification and
    // behaviour is identical to before -- old apps never see a burst. Zero new
    // GATT objects: same characteristic, just more sends.
    uint32_t remaining = (chunkOffset < fileLen)
            ? std::min<uint32_t>(fileLen - chunkOffset, chunkSize)
            : 0;

    mReadState.active = true;
    mReadState.command = command;
    mReadState.fileLen = fileLen;
    mReadState.offset = chunkOffset;
    // No overflow: remaining <= fileLen - chunkOffset.
    mReadState.windowEnd = chunkOffset + remaining;

    if (remaining == 0) {
        // Empty window (read at/after EOF): emit one terminating 0-length
        // response, exactly as the classic protocol did, then finalise.
        ReadFileResponse rsp {};
        rsp.status = Responce::OK;
        rsp.chunkOffset = chunkOffset;
        rsp.totalLength = fileLen;
        rsp.chunkLength = 0;
        memcpy(mResponseBuffer, &rsp, sizeof(rsp));
        sendResponce(mResponseBuffer, sizeof(rsp));
        finalizeReadWindow();
        return;
    }

    pumpRead();
}

void FileTransferService::pumpRead()
{
    ReadFileResponse rsp {};
    const uint32_t maxChunkLen = mBle.getMaxCharacteristicLen() - sizeof(rsp);

    // Emit responses for the current window until it is satisfied or the
    // controller TX pool is full. On a full pool trySetCharacteristicValue()
    // returns false and we PARK (return) without advancing offset;
    // una_ble_gatt_tx_pool_available_event -> onTxReady() calls back here the
    // instant a buffer frees. Single-threaded, non-blocking, no polling.
    while (mReadState.active && mReadState.offset < mReadState.windowEnd) {
        uint32_t len = std::min<uint32_t>(
                mReadState.windowEnd - mReadState.offset, maxChunkLen);

        rsp.status = Responce::OK;
        rsp.chunkOffset = mReadState.offset;
        rsp.totalLength = mReadState.fileLen;
        rsp.chunkLength = len;

        mFile->seek(mReadState.offset);
        size_t br = 0;
        if (!mFile->read(reinterpret_cast<char*>(&mResponseBuffer[sizeof(rsp)]),
                len, br) || br != len) {
            sendErr(mReadState.command, Responce::ERROR);
            mFile->close();
            mReadState.active = false;
            return;
        }

        memcpy(mResponseBuffer, &rsp, sizeof(rsp));

        bool poolFull = false;
        if (!mBle.trySetCharacteristicValue(mChrRawTransfer, mResponseBuffer,
                sizeof(rsp) + len, &poolFull)) {
            if (poolFull) {
                return;  // TX pool full: parked, resumed by onTxReady()
            }
            // Hard send error: no onTxReady() is coming, so parking would wedge
            // the window forever. Abort it like the read-failure path above; the
            // client re-paces from the last byte it actually received.
            sendErr(mReadState.command, Responce::ERROR);
            mFile->close();
            mReadState.active = false;
            return;
        }

        mReadState.offset += len;
    }

    // Window satisfied; report progress and, on EOF, finalise the transfer.
    finalizeReadWindow();
}

void FileTransferService::finalizeReadWindow()
{
    uint32_t readPosition = mReadState.offset;
    uint32_t fileLen = mReadState.fileLen;

    uint8_t pct = (readPosition >= fileLen)
            ? 100 : (uint8_t)(((uint64_t)100 * readPosition) / fileLen);

    // Whole file sent -> close and stop the transfer (mirrors the write path
    // closing at freeSpace 0). Otherwise wait for the client's next READ_PACING.
    if (readPosition >= fileLen) {
        mFile->close();
        mReadState.active = false;
    }

    if (pct >= mReadContext.sentPct + 25 || pct == 100) {   // every ~25% (was per-chunk)
        mReadContext.sentPct = pct;
        LOG_INFO("Reading: %u%% (%u/%u bytes) [%s]\n", pct, readPosition, fileLen, mFile->getPath());
    }

    if (mObserver) {
        mObserver->onFileRead(mFile->getPath(), pct);
    }
}

void FileTransferService::writeHandler(const uint8_t *pData, uint16_t len)
{
    bool status = false;
    uint8_t command = pData[0];

    WriteFileResponse rsp { };

    if (command == Command::WRITE) {
        WriteFileRequest req {};
        memcpy(&req, pData, sizeof(req));


        // Example 1. Empty file
        // path: /write.txt
        // length: 0
        // offset: 0
        //
        // 20 00 0A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2F 77 72 69 74 65 2E 74 78 74

        // Example 2. Content "Hello"
        // path: /write.txt
        // length: 5
        // offset: 0
        //
        // 20 00 0A 00 00 00 00 00 00 00 00 00 00 00 00 00 05 00 00 00 2F 77 72 69 74 65 2E 74 78 74
        // 22 01 00 00 00 00 00 00 05 00 00 00 48 65 6C 6C 6F

        // Example 3. Binaries with offset [xx xx 11 22 33 44 55 66 77 88]
        // path: /write.txt
        // length: 10
        // offset: 2
        //
        // 20 00 0A 00 02 00 00 00 00 00 00 00 00 00 00 00 0A 00 00 00 2F 77 72 69 74 65 2E 74 78 74
        // 22 01 00 00 02 00 00 00 06 00 00 00 11 22 33 44 55 66
        // 22 01 00 00 08 00 00 00 02 00 00 00 77 88


        // Validate command length
        uint16_t expectedLength = sizeof(req) + req.pathLength;
        if (len != expectedLength) {
            LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
            sendErr(command, Responce::ERROR_PROTOCOL);
            return;
        }

        LOG_DEBUG("Write start: offset %u, size %u [%.*s]\n", req.offset, req.totalSize, req.pathLength, &pData[sizeof(req)]);

        if (pData[sizeof(req)] != '/') {
            LOG_WARNING("Invalid path [%.*s]\n", req.pathLength, &pData[sizeof(req)]);
            sendErr(command, Responce::ERROR_PROTOCOL);
            return;
        }

        if (req.offset > req.totalSize) {
            LOG_WARNING("Invalid offset: Offset: %u, Total Size: %u [%.*s]\n",
                    req.offset, req.totalSize, req.pathLength, &pData[sizeof(req)]);
            sendErr(command, Responce::ERROR_PROTOCOL);
            return;
        }

        // This will close file if it was open
        mFile->setPath(TerminatePath(mPath, sizeof(mPath), &pData[sizeof(req)], req.pathLength));

        // Truncate (FA_CREATE_ALWAYS) only for a fresh write (offset 0). A resume
        // (offset > 0, e.g. after a disconnect) must PRESERVE the already-written
        // head [0, offset): open without truncation (FA_OPEN_ALWAYS) so the
        // subsequent seek(offset)+write overwrites from the seam onward. Passing
        // override=true unconditionally truncated the file on every WRITE, so a
        // resume wiped [0, offset) to a zero/undefined hole -- corruption the
        // high-water-mark ACK cannot see (it tracks contiguity from `offset`).
        status = mFile->open(true, req.offset == 0);
        if (!status) {
            LOG_ERROR("Failed to open file [%s]\n", mFile->getPath());
            sendErr(command, Responce::ERROR);
            return;
        }

        status = mFile->seek(req.offset);
        if (!status) {
            mFile->close();
            LOG_ERROR("Failed to set offset of %u bytes [%s]\n", req.offset, mFile->getPath());
            sendErr(command, Responce::ERROR);
            return;
        }

        uint32_t freeSpace = req.totalSize - req.offset;
        if (freeSpace == 0) {
            status = mFile->close();
        } else {
            status = mFile->flush();
            if (!status) {
                mFile->close();
            }
        }
        if(!status) {
            LOG_ERROR("Failed to save file [%s]\n", mFile->getPath());
            sendErr(command, Responce::ERROR);
            return;
        }

        mWriteContext.startTime = OS::GetTimeMs();
        mWriteContext.sentPct = 0;
        mWriteContext.lastNotifiedPct = 0;
        mWriteContext.totalSize = req.totalSize;
        mWriteContext.unflushed = 0;
        mWriteContext.contiguousEnd = req.offset;  // resume point / high-water-mark
        mWriteAck.pending = false;
        mWriteContext.time = 0;
        if (mFs.objectInfo(mFile->getPath(), mDirItem)) {
            mWriteContext.time = (uint64_t) mDirItem.utc * 1000000ULL;
        }

        rsp.status = Responce::OK;
        rsp.offset = req.offset;
        rsp.freeSpace = freeSpace;
        rsp.truncatedTime = mWriteContext.time;
        sendResponce((const uint8_t*)&rsp, sizeof(rsp));

        if (mObserver) {
            mObserver->onFileWrite(mFile->getPath(), 0);
        }

        if (freeSpace == 0) {
            LOG_INFO("Writing done [%s]\n", mFile->getPath());
            if (mObserver) {
                mObserver->onFileWrite(mFile->getPath(), 100);
            }
        }

    } else if (command == Command::WRITE_DATA) {

        WriteFileDataRequest req {};
        memcpy(&req, pData, sizeof(req));

        // Validate command length. 32-bit: req.dataSize is uint32, so a
        // truncated 16-bit sum could spuriously equal a small `len` and let
        // mFile->write() copy dataSize bytes from a short packet buffer (OOB
        // read). `len` is <= one ATT packet, so any oversized dataSize now
        // simply fails the comparison.
        uint32_t expectedLength = (uint32_t)sizeof(req) + req.dataSize;
        if (len != expectedLength) {
            LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
            sendErr(command, Responce::ERROR_PROTOCOL);
            return;
        }

        if (!mFile->isOpen()) {
            // A completed transfer closes the file. If the client is resending
            // the tail because the terminal WRITE_PACING was lost, re-send it
            // (freeSpace 0) so the client's ordinary go-back-N loop sees
            // completion -- no lost-final-ACK special case needed client-side.
            // `complete` is set only after the terminal close() committed this
            // exact transfer, and is cleared by any non-WRITE_DATA command, so
            // it can neither certify uncommitted data nor an unrelated transfer.
            // The bound (chunk within totalSize) is overflow-safe. sendResponce
            // blocks, but this is bounded (~one window of tail resends) and
            // post-transfer, so it cannot starve an in-flight write.
            if (mWriteContext.complete &&
                    req.offset <= mWriteContext.totalSize &&
                    req.dataSize <= mWriteContext.totalSize - req.offset) {
                WriteFileResponse done {};
                done.status = Responce::OK;
                done.offset = req.offset;
                done.truncatedTime = mWriteContext.time;
                done.freeSpace = 0;
                sendResponce((const uint8_t*)&done, sizeof(done));
                return;
            }
            LOG_WARNING("Invalid state. File not opened. Missed command 0x%02X [%s]\n", Command::WRITE, mFile->getPath());
            sendErr(command, Responce::ERROR);
            return;
        }

        // Overflow-safe bound (offset + dataSize can wrap uint32 for a hostile
        // offset). The contiguous high-water-mark below relies on this guard.
        if (req.dataSize > mWriteContext.totalSize ||
                req.offset > mWriteContext.totalSize - req.dataSize) {
            LOG_WARNING("Invalid offset: Offset (%u) + Data size (%u) > Total Size (%u) [%s]\n",
                    req.offset, req.dataSize, mWriteContext.totalSize, mFile->getPath());
            sendErr(command, Responce::ERROR_PROTOCOL);
            return;
        }

        LOG_DEBUG("Write chunk: offset %u, size %u [%s]\n", req.offset, req.dataSize, mFile->getPath());

        status = mFile->seek(req.offset);
        if (!status) {
            mFile->close();
            LOG_ERROR("Failed to set offset of %u bytes [%s]\n", req.offset, mFile->getPath());
            sendErr(command, Responce::ERROR);
            return;
        }

        size_t bw = 0;
        status = mFile->write(reinterpret_cast<const char*>(&pData[sizeof(req)]),
                req.dataSize, bw) && req.dataSize == bw;
        if (!status) {
            mFile->close();
            LOG_ERROR("Failed to write %u bytes [%s]\n", req.dataSize, mFile->getPath());
            sendErr(command, Responce::ERROR);
            return;
        }

        // Advance the contiguous high-water-mark only when this chunk has NO
        // gap before it (its start is at or below the mark and it extends the
        // mark). A dropped WRITE_DATA leaves a hole: later chunks are still
        // written at their own offset (sparse), but contiguousEnd stays pinned
        // at the first missing byte, so freeSpace (= totalSize - contiguousEnd)
        // stops advancing there. The client sees the stall and retransmits from
        // (totalSize - freeSpace) go-back-N; the terminal ACK (freeSpace == 0)
        // is therefore only ever sent for a hole-free file. Reporting the raw
        // chunk end here instead would let the terminal ACK fire over holes.
        if (req.offset <= mWriteContext.contiguousEnd &&
                req.offset + req.dataSize > mWriteContext.contiguousEnd) {
            mWriteContext.contiguousEnd = req.offset + req.dataSize;
        }
        uint32_t freeSpace = mWriteContext.totalSize - mWriteContext.contiguousEnd;

        // Batch the eMMC flush: f_sync every skFlushThreshold bytes rather than
        // on every chunk. Per-chunk f_sync was a throughput sink and put the
        // SDMMC busy-wait on the OTA hot path (a hang mid-OTA is the dangerous
        // case). Draining writes this quickly is also what lets the watch keep
        // up with a windowed (streamed) write burst without losing controller
        // events. The durability window is bounded by the threshold; the final
        // chunk closes (which commits), and onDisconnect() closes any partial
        // transfer. `status` is already true from the successful write above, so
        // a deferred flush leaves it unchanged.
        static constexpr uint32_t skFlushThreshold = 8192;
        mWriteContext.unflushed += req.dataSize;
        if (freeSpace == 0) {
            status = mFile->close();
            // Mark complete only once the close (which commits the tail to
            // eMMC) succeeds -- so the re-ACK below never certifies uncommitted
            // data. A failed close leaves complete=false and errors out; the
            // client's rewound tail then retries the close via the normal path.
            if (status) {
                mWriteContext.complete = true;
            }
        } else if (mWriteContext.unflushed >= skFlushThreshold) {
            status = mFile->flush();
            if (status) {
                mWriteContext.unflushed = 0;
            } else {
                mFile->close();
            }
        }
        if(!status) {
            LOG_ERROR("Failed to save file [%s]\n", mFile->getPath());
            sendErr(command, Responce::ERROR);
            return;
        }

        rsp.status = Responce::OK;
        rsp.offset = req.offset;
        rsp.freeSpace = freeSpace;
        rsp.truncatedTime = mWriteContext.time;
        // Non-blocking ACK. On a full TX pool, park the (cumulative) ACK and let
        // onTxReady() deliver it when a buffer frees. Blocking here would stall
        // the HCI thread that must drain a streamed (windowed) write burst,
        // overflowing the controller event queue -> dropped WRITE_DATA ->
        // corruption. A newer pending ACK supersedes an older one.
        static_assert(sizeof(rsp) <= sizeof(mWriteAck.data), "write ACK too large");
        bool poolFull = false;
        if (mBle.trySetCharacteristicValue(mChrRawTransfer,
                (const uint8_t*)&rsp, sizeof(rsp), &poolFull)) {
            mWriteAck.pending = false;
        } else if (poolFull) {
            memcpy(mWriteAck.data, &rsp, sizeof(rsp));
            mWriteAck.len = sizeof(rsp);
            mWriteAck.pending = true;
        } else {
            // Not pool-full: no onTxReady() will follow, so parking would hang a
            // stop-and-wait client waiting on this ACK. This is the rare
            // transient-error path (the hot burst path returns pool-full/0x64),
            // so fall back to the blocking sender -- it rides out a transient
            // and surfaces a genuine failure, matching classic behaviour.
            mWriteAck.pending = false;
            sendResponce((const uint8_t*)&rsp, sizeof(rsp));
        }

        uint32_t writePosition = mWriteContext.totalSize - freeSpace;
        uint8_t pct = (uint8_t)(((uint64_t)100 * writePosition) / mWriteContext.totalSize);

        // GUI progress bar: notify on every 1% change so the bar advances
        // smoothly (this is the on-watch OTA progress). Cheap -- it posts an
        // event and does NOT touch the UART, unlike the log below.
        if (mObserver && pct != mWriteContext.lastNotifiedPct) {
            mWriteContext.lastNotifiedPct = pct;
            mObserver->onFileWrite(mFile->getPath(), pct);
        }

        // Log every ~25% only: per-chunk logging (~100 lines) ran on the HCI
        // event thread and added jitter/eMMC-UART contention during the tightest
        // part of a fast transfer.
        if (pct >= mWriteContext.sentPct + 25 || pct == 100) {
            mWriteContext.sentPct = pct;
            LOG_INFO("Writing: %u%% (%u/%u bytes) [%s]\n", pct, writePosition, mWriteContext.totalSize, mFile->getPath());
            if (freeSpace == 0) {
                uint32_t endTime = OS::GetTimeMs();
                if (endTime > mWriteContext.startTime) {
                    float speed = (mWriteContext.totalSize * 1000.0f) / ((endTime - mWriteContext.startTime) * 1024);
                    LOG_INFO("Writing done. Time %.2f s, Speed %.2f kB/s [%s]\n",
                            (endTime - mWriteContext.startTime)/1000.0f, speed, mFile->getPath());
                } else {
                    LOG_INFO("Writing done [%s]\n", mFile->getPath());
                }
            }
        }

    }
}

void FileTransferService::deleteHandler(const uint8_t *pData, uint16_t len)
{
    // path: /write.txt
    // 30 00 0A 00 2F 77 72 69 74 65 2E 74 78 74

    uint8_t command = pData[0];
    DeleteFileResponse rsp {};

    DeleteFileRequest req { };
    memcpy(&req, pData, sizeof(req));

    // Validate command length
    uint16_t expectedLength = sizeof(req) + req.pathLength;
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendErr(command, Responce::ERROR_PROTOCOL);
        return;
    }

    if (req.pathLength <= 1 || pData[sizeof(req)] != '/') {
        LOG_WARNING("Invalid path [%.*s]\n", req.pathLength, &pData[sizeof(req)]);
        sendErr(command, Responce::ERROR_PROTOCOL);
        return;
    }

    const char *pPath = TerminatePath(mPath, sizeof(mPath), &pData[sizeof(req)], req.pathLength);

    if (!mFs.remove(pPath)) {
        LOG_ERROR("Failed to remove object [%s]\n", pPath);
        sendErr(command, Responce::ERROR);
    } else {
        rsp.status = Responce::OK;
        sendResponce((const uint8_t*)&rsp, sizeof(rsp));
        LOG_INFO("Object removed [%s]\n", pPath);
    }
}

void FileTransferService::makeDirHandler(const uint8_t *pData, uint16_t len)
{
    // path: /Dir/dir
    // 40 00 08 00 00 00 00 00 18 6E C6 52 80 DC 94 00 2F 44 69 72 2F 64 69 72

    uint8_t command = pData[0];
    MakeDirectoryResponse rsp {};

    MakeDirectoryRequest req { };
    memcpy(&req, pData, sizeof(req));

    // Validate command length
    uint16_t expectedLength = sizeof(req) + req.pathLength;
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendErr(command, Responce::ERROR_PROTOCOL);
        return;
    }

    if (pData[sizeof(req)] != '/') {
        LOG_WARNING("Invalid path [%.*s]\n", req.pathLength, &pData[sizeof(req)]);
        sendErr(command, Responce::ERROR_PROTOCOL);
        return;
    }

    const char *pPath = TerminatePath(mPath, sizeof(mPath), &pData[sizeof(req)], req.pathLength);

    mDir->setPath(pPath);
    if (!mDir->create()) {
        sendErr(command, Responce::ERROR);
        LOG_ERROR("Failed to create directory [%s]\n", pPath);
        return;
    }

    if (mFs.objectInfo(pPath, mDirItem)) {
        rsp.truncatedTime = (uint64_t) mDirItem.utc * 1000000000ULL;
    }

    rsp.status = Responce::OK;
    sendResponce((const uint8_t*)&rsp, sizeof(rsp));

    LOG_INFO("Directory created [%s]\n", pPath);
}

void FileTransferService::listDirHandler(const uint8_t *pData, uint16_t len)
{
    // path: /
    // 50 00 01 00 2F

    uint8_t command = pData[0];
    ListDirectoryResponse rsp {};
    // Guard the subtraction: at the spec-minimum MTU (23) the sendable payload
    // is 20 bytes, smaller than this response header (28). Left unsigned this
    // underflows to ~4 GB, defeating the pathLength bound below and overflowing
    // mResponseBuffer on a long filename. 0 => every non-empty name is rejected
    // as too long, which is correct (no entry fits at that MTU).
    const uint32_t cap = mBle.getMaxCharacteristicLen();
    const uint32_t maxPathLen = (cap > sizeof(rsp)) ? (cap - sizeof(rsp)) : 0;

    ListDirectoryRequest req { };
    memcpy(&req, pData, sizeof(req));

    // Validate command length
    uint16_t expectedLength = sizeof(req) + req.pathLength;
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendErr(command, Responce::ERROR_PROTOCOL);
        return;
    }

    if (pData[sizeof(req)] != '/') {
        LOG_WARNING("Invalid path [%.*s]\n", req.pathLength, &pData[sizeof(req)]);
        sendErr(command, Responce::ERROR_PROTOCOL);
        return;
    }

    mDir->setPath(TerminatePath(mPath, sizeof(mPath), &pData[sizeof(req)], req.pathLength));

    if (req.pathLength > 1 // Not a root directory
            && !mDir->exist()) {
        sendErr(command, Responce::ERROR_NO_FILE);
        LOG_WARNING("Directory not exist [%s]\n", mDir->getPath());
        return;
    }

    uint32_t i = 0;
    uint32_t totalNum = 0;

    LOG_INFO("List directory [%s]\n", mDir->getPath());

    if (mDir->open()) {
        // Get items count
        while (mDir->readNext(mDirItem)) {
            totalNum++;

            if (totalNum > 255) {
                mDir->close();
                LOG_ERROR("Cycling detected [%s]\n", mDir->getPath());
                sendErr(command, Responce::ERROR);
                return;
            }
        }

        mDir->readNext(mDirItem, true);  // Reset index

        while (mDir->readNext(mDirItem)) {
            rsp.status = Responce::OK;
            rsp.pathLength = strlen(mDirItem.name);
            rsp.entryNumber = i++;
            rsp.totalEntries = totalNum;
            rsp.flags = mDirItem.isDir ? 1 : 0;
            rsp.modificationTime = (uint64_t)mDirItem.utc * 1000000ULL;
            rsp.fileSize = mDirItem.size;

            if (rsp.pathLength > maxPathLen) {
                LOG_ERROR("Path is too long [%s]\n", mDirItem.name);
                rsp.status = Responce::ERROR;
            } else {
                LOG_DEBUG("[%s%s]\n", mDir->getPath(), mDirItem.name);
                memcpy(&mResponseBuffer[sizeof(rsp)], mDirItem.name, rsp.pathLength);
            }
            memcpy(mResponseBuffer, &rsp, sizeof(rsp));

            sendResponce(mResponseBuffer, sizeof(rsp) + rsp.pathLength);
        }

        // Final entry
        rsp.status = Responce::OK;
        rsp.pathLength = 0;
        rsp.entryNumber = totalNum;
        rsp.totalEntries = totalNum;
        rsp.flags = 0;
        rsp.modificationTime = 0;
        rsp.fileSize = 0;
        sendResponce((const uint8_t*)&rsp, sizeof(rsp));

        mDir->close();
    } else {
        LOG_ERROR("Failed to open directory [%s]\n", mDir->getPath());
        sendErr(command, Responce::ERROR);
    }

}

void FileTransferService::moveHandler(const uint8_t *pData, uint16_t len)
{
    uint8_t command = pData[0];
    MoveResponse rsp {};

   MoveRequest req { };
   memcpy(&req, pData, sizeof(req));

   // Validate command length
   uint16_t expectedLength = sizeof(req) + req.oldPathLength + 1 + req.newPathLength;
   if (len != expectedLength) {
       LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
       sendErr(command, Responce::ERROR_PROTOCOL);
       return;
   }

   const char *pOld = (const char *)&pData[sizeof(req)];
   const char *pNew = (const char *)&pData[sizeof(req) + req.oldPathLength + 1];

   if (req.oldPathLength <= 1 || pOld[0] != '/') {
       LOG_ERROR("Invalid old path [%.*s]\n", req.oldPathLength, pOld);
       sendErr(command, Responce::ERROR_PROTOCOL);
       return;
   }

   if (req.newPathLength <= 1 || pNew[0] != '/') {
       LOG_ERROR("Invalid new path [%.*s]\n", req.newPathLength, pNew);
       sendErr(command, Responce::ERROR_PROTOCOL);
       return;
   }

   char oldPath[req.oldPathLength + 1] {};
   TerminatePath(oldPath, sizeof(oldPath), (const uint8_t*)pOld, req.oldPathLength);

   const char *pNewPath = TerminatePath(mPath, sizeof(mPath), (const uint8_t*)pNew, req.newPathLength);

   if (mFs.rename(oldPath, pNewPath)) {
       rsp.status = Responce::OK;
       sendResponce((const uint8_t*)&rsp, sizeof(rsp));
       LOG_INFO("Object renamed [%s -> %s]\n", oldPath, pNewPath);
   } else {
       sendErr(command, Responce::ERROR);
       LOG_ERROR("Failed to rename object [%s -> %s]\n", oldPath, pNewPath);
   }
}

void FileTransferService::digestHandler(const uint8_t *pData, uint16_t len)
{
    uint8_t command = pData[0];
    DigestResponse rsp {};

    DigestRequest req {};
    memcpy(&req, pData, sizeof(req));

    uint16_t expectedLength = sizeof(req) + req.pathLength;
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendErr(command, Responce::ERROR_PROTOCOL);
        return;
    }

    if (req.pathLength <= 1 || pData[sizeof(req)] != '/') {
        LOG_WARNING("Invalid path [%.*s]\n", req.pathLength, &pData[sizeof(req)]);
        sendErr(command, Responce::ERROR_PROTOCOL);
        return;
    }

    // setPath closes any file a prior transfer left open.
    mFile->setPath(TerminatePath(mPath, sizeof(mPath), &pData[sizeof(req)], req.pathLength));

    if (!mFile->exist()) {
        sendErr(command, Responce::ERROR_NO_FILE);
        LOG_ERROR("File not exist [%s]\n", mFile->getPath());
        return;
    }
    if (!mFile->open()) {
        sendErr(command, Responce::ERROR);
        LOG_ERROR("Failed to open file [%s]\n", mFile->getPath());
        return;
    }

    // Stream the whole file through standard CRC-32 (IEEE/zlib) locally -- no BLE
    // payload, just an eMMC read. Reuse mResponseBuffer as read scratch; the
    // 12-byte response is built afterwards. CRC32::calculate chains via the
    // running crc (initial 0), matching java.util.zip.CRC32 / zlib.crc32.
    uint32_t crc = 0;
    uint32_t total = 0;
    for (;;) {
        size_t br = 0;
        if (!mFile->read(reinterpret_cast<char*>(mResponseBuffer),
                sizeof(mResponseBuffer), br)) {
            mFile->close();
            sendErr(command, Responce::ERROR);
            LOG_ERROR("Digest read failed [%s]\n", mFile->getPath());
            return;
        }
        if (br == 0) {
            break;   // EOF
        }
        crc = CRC32::calculate(mResponseBuffer, (uint32_t)br, crc);
        total += (uint32_t)br;
    }
    mFile->close();

    rsp.status = Responce::OK;
    rsp.fileSize = total;
    rsp.crc32 = crc;
    sendResponce((const uint8_t*)&rsp, sizeof(rsp));

    LOG_INFO("Digest [%s]: %u bytes, CRC32 0x%08lX\n",
            mFile->getPath(), total, (unsigned long)crc);
}

bool FileTransferService::sendResponce(const uint8_t *pData, uint16_t len)
{
    LOG_DEBUG("Response %d bytes:\n", len);
    LOG_DEBUG_DUMP(pData, len);
    return mBle.setCharacteristicValue(mChrRawTransfer, (const uint8_t*) pData, len);
}

void FileTransferService::sendErr(uint8_t cmd, uint8_t code)
{
    mResponseBuffer[0] = cmd;
    mResponseBuffer[1] = code;

    LOG_DEBUG("Status 2 bytes:\n");
    LOG_DEBUG_DUMP(mResponseBuffer, 2);

    sendResponce(mResponseBuffer, 2);
}

}  /* namespace Ble */
