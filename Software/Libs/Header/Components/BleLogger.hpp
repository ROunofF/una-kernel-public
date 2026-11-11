/**
 ******************************************************************************
 * @file    BleLogger.hpp
 * @brief   Asynchronous BLE logger.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef BLE_LOGGER_HPP
#define BLE_LOGGER_HPP

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string_view>

#include "OS.hpp"
#include "Services/NordicUartService.hpp"

/**
 * @brief Asynchronous BLE logger with automatic buffering
 *
 * Provides thread-safe asynchronous writing to BLE using
 * internal stream buffer and dedicated worker thread
 */
class BleLogger : virtual public Ble::NordicUartService::Callback {
public:

    static BleLogger& GetInstance()
    {
        static BleLogger sInstance;
        return sInstance;
    }

    void start(Ble::NordicUartService &nus)
    {
        if (mThread.isRun()) {
            return;
        }
        mNus = &nus;
        mStreamBuffer.init("BleLogger");
        mThread.init("BleLogger", this, &BleLogger::thread, osPriorityNormal, 500);
    }

    void stop()
    {
        if (!mThread.isRun()) {
            return;
        }
        mThread.requestStop();
        mThread.join(5000);
        mThread.deinit();
        mStreamBuffer.deinit();

        mNus = nullptr;
    }

    /**
     * @brief Write raw data
     * @param data Pointer to data buffer
     * @param size Size of data in bytes
     * @retval true Data queued successfully
     * @retval false Thread not running or queue full (record dropped whole)
     */
    bool write(const void *data, size_t size)
    {
        static const char    kDropMark[]  = "\n<records dropped>\n";
        constexpr uint32_t   kDropMarkLen = sizeof(kDropMark) - 1;

        if (!mThread.isRun()) {
            return false;
        }

        // Non-blocking and all-or-nothing: callers include sensor data
        // callbacks that run inside the sensor manager's refresh sweep, so a
        // full queue must drop, never block — and a partial send would break
        // the log framing. The stream buffer is single-writer and the drain
        // only frees space, so the precheck cannot be invalidated before
        // the send.
        const uint32_t needed = size + (mDropPending ? kDropMarkLen : 0);
        if (mStreamBuffer.getFreeSpace() < needed) {
            mDropPending = true;
            return false;
        }

        // One marker per gap, so the reader sees where records were lost.
        if (mDropPending) {
            mStreamBuffer.send(reinterpret_cast<const uint8_t*>(kDropMark),
                               kDropMarkLen, 0);
            mDropPending = false;
        }

        return mStreamBuffer.send(static_cast<const uint8_t*>(data),
                                  size, 0) == size;
    }

private:
    void thread()
    {
        OS::ThreadInitGuard initGuard(mThread);
        initGuard.success();
        OS::CancellableGuard guard(&mThread, &mStreamBuffer);

        while (!mThread.shouldStop()) {
            size_t br = mStreamBuffer.receive(
                reinterpret_cast<uint8_t*>(mBuff),
                sizeof(mBuff)
            );

            if (mThread.shouldStop()) {
                break;
            }

            if (br > 0 && mNus) {
                mNus->onTx({reinterpret_cast<const char*>(mBuff), static_cast<size_t>(br)});
            }
        }
    }

    Ble::NordicUartService *mNus = nullptr;
    OS::Thread<BleLogger, 2 * 1024> mThread;
    OS::StreamBuffer<4*1024> mStreamBuffer;
    char mBuff[128];
    bool mDropPending = false;   ///< A drop-gap marker is owed to the stream.

    virtual void onRx(std::string_view str) override {}
};

#endif // BLE_LOGGER_HPP
