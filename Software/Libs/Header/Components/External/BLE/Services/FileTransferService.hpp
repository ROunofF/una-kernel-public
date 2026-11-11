/**
 ******************************************************************************
 * @file    FileTransferService.hpp
 * @brief   BLE File Transfer Service (FTS).
 *
 * @note    This is an implementation of a simple protocol that works through
 *          the GATT layer:
 *          https://github.com/adafruit/Adafruit_CircuitPython_BLE_File_Transfer
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_SERVICE_FTS_HPP
#define __COMPONENT_BLE_SERVICE_FTS_HPP

#include "IBle.hpp"
#include "IFileSystem.hpp"

namespace Ble
{

/**
 * @brief   BLE File Transfer Service (FTS).
 */
class FileTransferService : virtual public Interface::IBle::StatusCallback,
        virtual public Interface::IBle::CharacteristicCallback {
public:

    class Callback {
    public:
        virtual void onFileWrite(const char *path, uint8_t pct){}
        virtual void onFileRead(const char *path, uint8_t pct){}
    protected:
        virtual ~Callback() = default;
    };


    /**
     * @brief   Constructor.
     * @param   ble: BLE instance.
     * @param   pCallback: File operation callback.
     * @param   fs: File system interface.
     */
    FileTransferService(Interface::IBle &ble, Callback *pCallback, Interface::IFileSystem &fs);

    /**
     * @brief   Destructor.
     */
    virtual ~FileTransferService();

private:

    Interface::IBle &mBle;
    Callback *mObserver = nullptr;
    Interface::IFileSystem &mFs;


    ServiceHandle mSrv {};
    CharacteristicHandle mChrVersion {};
    CharacteristicHandle mChrRawTransfer {};

    bool mNotificationEnabled = false;

    char mPath[Interface::IFileSystem::skMaxPathLen] {};
    std::unique_ptr<Interface::IFile> mFile;
    std::unique_ptr<Interface::IDirectory> mDir;
    Interface::IFileSystem::ObjectInfo mDirItem {};


    struct {
        uint8_t sentPct;
    } mReadContext { };

    // Event-driven read window state machine (see pumpRead()). A read request
    // sets [offset, windowEnd); pumpRead() emits it as a burst, parking on a
    // full TX pool and resuming from onTxReady().
    struct {
        bool active = false;
        uint8_t command = 0;
        uint32_t offset = 0;
        uint32_t windowEnd = 0;
        uint32_t fileLen = 0;
    } mReadState { };

    struct {
        uint32_t startTime;
        uint32_t totalSize;
        uint32_t unflushed;      // bytes written since the last eMMC flush
        uint32_t contiguousEnd;  // highest offset with NO gap before it; the
                                 // WRITE_PACING freeSpace is totalSize - this,
                                 // so a dropped WRITE_DATA stalls the ACK at the
                                 // first missing byte (client retransmits from
                                 // there).
        uint64_t time;
        uint8_t sentPct;         // last %-point logged (sparse, ~25%)
        uint8_t lastNotifiedPct; // last %-point sent to the GUI observer (per 1%)
        bool complete;           // set true ONLY after the terminal close()
                                 // commits (=> data on disk, hole-free); gates
                                 // the lost-terminal-ACK re-ACK. Cleared by any
                                 // non-WRITE_DATA command so a stale completion
                                 // can't certify a later transfer.
    } mWriteContext { };

    // Latest WRITE_DATA ACK deferred because the TX pool was full; delivered
    // from onTxReady() when a buffer frees, so the ACK send never blocks the
    // HCI thread draining a streamed (windowed) write burst. ACKs are
    // cumulative (freeSpace), so one pending ACK supersedes any earlier one.
    struct {
        bool pending = false;
        uint16_t len = 0;
        uint8_t data[32] {};
    } mWriteAck { };

    uint8_t mResponseBuffer[256] {};




    // FTS protocol version reported on the ADAF0001 (Version) characteristic.
    // Bumped 4 -> 5 for the fast-transfer capabilities (read windowing, credit-
    // based write windowing, DIGEST). A client reading >= 5 may use them; a v4
    // client sees classic stop-and-wait behaviour, and a deployed v4 app
    // tolerates the higher value (verified on-device).
    static constexpr uint32_t skProtocolVersion = 5;

    struct Command {
        enum {
            INVALID = 0x00,
            READ = 0x10,
            READ_DATA = 0x11,
            READ_PACING = 0x12,
            WRITE = 0x20,
            WRITE_PACING = 0x21,
            WRITE_DATA = 0x22,
            DELETE = 0x30,
            DELETE_STATUS = 0x31,
            MKDIR = 0x40,
            MKDIR_STATUS = 0x41,
            LISTDIR = 0x50,
            LISTDIR_ENTRY = 0x51,
            MOVE = 0x60,
            MOVE_STATUS = 0x61,
            DIGEST = 0x70,
            DIGEST_STATUS = 0x71,
        };
    };

    struct Responce {
        enum {
            INVALID = 0x00,
            OK = 0x01,
            ERROR = 0x02,
            ERROR_NO_FILE = 0x03,
            ERROR_PROTOCOL = 0x04,
            ERROR_READ_ONLY = 0x05,
        };
    };

    // 0x10 - Read a file (Request)
    struct ReadFileRequest {
        uint8_t command = 0x10;
        uint8_t reserved = 0;   // padding
        uint16_t pathLength;    // length of the path
        uint32_t chunkOffset;   // file offset to start reading
        uint32_t chunkSize;     // size of the chunk to read
    } __attribute__((packed));

    // 0x11 - Read a file (Response)
    struct ReadFileResponse {
        uint8_t command = 0x11;
        uint8_t status;
        uint8_t reserved[2] = {0};  // padding
        uint32_t chunkOffset;       // offset into the file
        uint32_t totalLength;       // total file length
        uint32_t chunkLength;       // length of this chunk
    } __attribute__((packed));

    // 0x12 - Request more data
    struct ReadFileMoreDataRequest {
        uint8_t command = 0x12;
        uint8_t status = 0x01;      // Always OK for now.
        uint8_t reserved[2] = {0};  // padding
        uint32_t chunkOffset;       // offset into the file
        uint32_t chunkSize;         // size of the chunk to read
    } __attribute__((packed));

    // 0x20 - Write a file (Request)
    struct WriteFileRequest {
        uint8_t command = 0x20;
        uint8_t reserved = 0;   // padding
        uint16_t pathLength;    // length of the path
        uint32_t offset;        // file offset to start writing
        uint64_t currentTime;   // nanoseconds since 1970
        uint32_t totalSize;     // total file size
    } __attribute__((packed));;

    // 0x21 - Write file (Response)
    struct WriteFileResponse {
        uint8_t command = 0x21;
        uint8_t status;             // execution status
        uint8_t reserved[2] = {0};  // padding
        uint32_t offset;            // file offset to start writing
        uint64_t truncatedTime;     // time from file system
        uint32_t freeSpace;         // free space for more data
    } __attribute__((packed));;

    // 0x22 - Write file data (Request)
    struct WriteFileDataRequest {
        uint8_t command = 0x22;
        uint8_t status = 0x01;      // Always OK for now.
        uint8_t reserved[2] = {0};  // padding
        uint32_t offset;            // offset in file
        uint32_t dataSize;          // size of the data chunk
    } __attribute__((packed));;

    // 0x30 - Delete a file or directory (Request)
    struct DeleteFileRequest {
        uint8_t command = 0x30;
        uint8_t reserved = 0;   // padding
        uint16_t pathLength;    // length of the path
    } __attribute__((packed));

    // 0x31 - Delete file or directory (Response)
    struct DeleteFileResponse {
        uint8_t command = 0x31;
        uint8_t status;         // 0x01 if deleted, 0x05 if read-only, 0x02 if non-existent
    } __attribute__((packed));

    // 0x40 - Make a directory (Request)
    struct MakeDirectoryRequest {
        uint8_t command = 0x40;
        uint8_t reserved = 0;   // padding
        uint16_t pathLength;    // length of the path
        uint32_t reserved2 = 0; // additional padding
        uint64_t currentTime;   // nanoseconds since 1970
    } __attribute__((packed));

    // 0x41 - Make a directory (Response)
    struct MakeDirectoryResponse {
        uint8_t command = 0x41;
        uint8_t status;
        uint8_t reserved[6] = {0};  // padding
        uint64_t truncatedTime;     // truncated time for caching
    } __attribute__((packed));

    // 0x50 - List a directory (Request)
    struct ListDirectoryRequest {
        uint8_t command = 0x50;
        uint8_t reserved = 0;   // padding
        uint16_t pathLength;    // length of the path
    } __attribute__((packed));

    // 0x51 - List directory (Response)
    struct ListDirectoryResponse {
        uint8_t command = 0x51;
        uint8_t status;
        uint16_t pathLength;
        uint32_t entryNumber;
        uint32_t totalEntries;
        uint32_t flags;
        uint64_t modificationTime;
        uint32_t fileSize;      // ignore for directories
    } __attribute__((packed));

    // 0x60 - Move a file or directory (Request)
    struct MoveRequest {
        uint8_t command = 0x60;
        uint8_t reserved = 0;  // padding
        uint16_t oldPathLength;
        uint16_t newPathLength;
     } __attribute__((packed));

    // 0x61 - Move file or directory (Response)
    struct MoveResponse {
        uint8_t command = 0x61;
        uint8_t status;         // 0x01 for success, 0x05 for read-only, 0x02 for other errors
    } __attribute__((packed));

    // 0x70 - File digest / CRC-32 (Request)
    struct DigestRequest {
        uint8_t command = 0x70;
        uint8_t reserved = 0;   // padding
        uint16_t pathLength;    // length of the path
    } __attribute__((packed));

    // 0x71 - File digest (Response)
    struct DigestResponse {
        uint8_t command = 0x71;
        uint8_t status;
        uint8_t reserved[2] = {0};  // padding
        uint32_t fileSize;          // total bytes hashed
        uint32_t crc32;             // standard CRC-32 (IEEE/zlib) of the file
    } __attribute__((packed));


    /**
     * @copydoc Interface::IBle::StatusCallback::onEnable()
     */
    virtual void onEnable() override;

    /**
     * @copydoc Interface::IBle::StatusCallback::onDisconnect()
     */
    virtual void onDisconnect(uint8_t reason) override;

    /**
     * @copydoc Interface::IBle::CharacteristicCallback::onNotifyIndicate()
     */
    virtual void onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
            bool notify, bool indicate) override;

    /**
     * @copydoc Interface::IBle::CharacteristicCallback::onWrite()
     */
    virtual void onWrite(Ble::CharacteristicHandle chrHandle,
            const uint8_t *pData, uint16_t len) override;

    /**
     * @copydoc Interface::IBle::StatusCallback::onTxReady()
     */
    virtual void onTxReady() override;


    static const char* TerminatePath(char *dst, uint16_t dstSize,
            const uint8_t *pPath, uint16_t pathLen);

    void readHandler(const uint8_t *pData, uint16_t len);
    void pumpRead();
    void finalizeReadWindow();

    void writeHandler(const uint8_t *pData, uint16_t len);
    void deleteHandler(const uint8_t *pData, uint16_t len);
    void makeDirHandler(const uint8_t *pData, uint16_t len);
    void listDirHandler(const uint8_t *pData, uint16_t len);
    void moveHandler(const uint8_t *pData, uint16_t len);
    void digestHandler(const uint8_t *pData, uint16_t len);

    bool sendResponce(const uint8_t *pData, uint16_t len);
    void sendErr(uint8_t cmd, uint8_t code);

};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_SERVICE_FTS_HPP */
