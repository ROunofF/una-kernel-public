/**
 ******************************************************************************
 * @file    AirohaBase.hpp
 * @brief   Base class for Airoha GPS.
 ******************************************************************************
 *
 ******************************************************************************
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <variant>
#include <string_view>
#include <functional>
#include <ctime>
#include <memory>


#include "IUart.hpp"
#include "IGpio.hpp"
#include "OS.hpp"

#include "AG3335/AirohaCommandContext.hpp"
#include "AG3335/AirohaContextPool.hpp"
#include "AG3335/AirohaType.hpp"
#include "AG3335/AirohaEpoParser.hpp"
#include "AG3335/AirohaNmeaParser.hpp"


namespace Airoha {

class AirohaBase
{
public:

    /**
     * @brief   RAW data callbacks interface
     */
    class RawDataCallback {
    public:
        virtual void onRawDataBin(uint8_t* data, size_t size) {}
        virtual void onRawDataRx(std::string_view rx) {}
        virtual void onRawTokensRx(std::string_view* tokens, size_t count) {}
        virtual void onRawDataTx(std::string_view tx) {}
    protected:

        /**
         * @brief   Destructor.
         */
        virtual ~RawDataCallback() = default;
    };

    /**
     * @brief   Aiding request callbacks interface
     */
    class AidingCallback {
    public:

        /**
         * @brief   Called when GPS requested reference time.
         * @param   utc: reference to save UTC time.
         * @retval  Execution status. 'true' if time is available, 'false' otherwise.
         */
        virtual bool onAidingRefTimeRequest(std::tm &utc) { return false; }

        /**
         * @brief   Called when GPS requested reference location.
         * @param   loc: reference to save reference location.
         * @retval  Execution status. 'true' if location is available, 'false' otherwise.
         */
        virtual bool onAidingRefLocationRequest(ReferenceLocation &loc) { return false; }

        /**
         * @brief   Called when GPS wants to save reference location.
         * @param   lat: latitude in degrees.
         * @param   lon: longitude in degrees.
         * @param   alt: Reference height in meters.
         */
        virtual void onAidingRefLocationSave(float lat, float lon, float alt) {}

        /**
         * @brief   Called when GPS requested EPO.
         * @param   sys: The GNSS system type.
         * @param   week: The current GNSS week number.
         * @param   tow: The current GNSS time of week.
         * @retval  The header and the segments to inject for the current
         *          window; empty if there is nothing to send.
         */
        virtual EpoData onAidingEpoRequest(SystemType sys, uint32_t week, uint32_t tow) { return EpoData {}; }

    protected:

        /**
         * @brief   Destructor.
         */
        virtual ~AidingCallback() = default;
    };

    /**
     * @brief   NMEA callbacks interface
     */
    class NmeaCallback {
    public:
        virtual void onNmeaGGA(const Nmea::GGA& cmd) {}
        virtual void onNmeaRMC(const Nmea::RMC& cmd) {}
        virtual void onNmeaGSV(const Nmea::GSV& cmd) {}

    protected:

        /**
         * @brief   Destructor.
         */
        virtual ~NmeaCallback() = default;
    };

    AirohaBase();
    AirohaBase(Interface::IUart &uart, Interface::IGpo &pwr);
    virtual ~AirohaBase();

    bool config(Interface::IUart &uart, Interface::IGpo &pwr);

    void setRawDataCallback(RawDataCallback *cb) { mRawDataCallback = cb; }
    void setAidingCallback(AidingCallback *cb) { mAidingCallback = cb; }
    void setNmeaCallback(NmeaCallback *cb) { mNmeaCallback = cb; }

    bool init();
    bool deinit();
    bool isInitialized() const;

    bool powerOn(uint32_t timeout = 0);
    // After this command, all others will be ignored except powerOn
    bool powerOff(uint32_t timeout = 0);
    // NOTE: The state may not update instantly because the powerOn/powerOff command is queued.
    // For reliable results, use powerOn/powerOff in blocking mode.
    bool isPowerOn();

    // Waiting for PAIR_INDICATION_SYSTEM_MESSAGE ($PAIR011,001*27)
    // It is sends after power on, reboot and after starts (COLD/WARM/HOT) ...
    bool waitForStartupGNSS(uint32_t timeout = 1000);


protected:
    bool mUseL1Only = false;
    ContextPool mContextPool;

    bool cmdGeneric(std::string_view cmdId, uint32_t timeout, bool blocking);
    bool cmdGenericSetBOOL(std::string_view cmdId, bool v, uint32_t timeout, bool blocking);
    bool cmdGenericGetBOOL(std::string_view cmdId, bool &v, uint32_t timeout);
    bool cmdGenericSetUINT(std::string_view cmdId, uint32_t v, uint32_t timeout, bool blocking);
    bool cmdGenericGetUINT(std::string_view cmdId, uint32_t &v, uint32_t timeout);
    bool cmdGenericSetINT(std::string_view cmdId, int32_t v, uint32_t timeout, bool blocking);
    bool cmdGenericGetINT(std::string_view cmdId, int32_t &v, uint32_t timeout);
    bool cmdGenericSetFLOAT(std::string_view cmdId, float v, uint8_t presigion, uint32_t timeout, bool blocking);
    bool cmdGenericGetFLOAT(std::string_view cmdId, float &v, uint32_t timeout);


    /**
     * @brief Send command without waiting for response
     * @param ctx Command context (acquired from pool, owned by caller)
     * @retval true Command queued successfully
     * @retval false Queue full
     *
     * @par Ownership:
     * Caller ALWAYS retains ownership and MUST call ctx->release() after
     * this function returns, regardless of success or failure.
     *
     * @code
     * auto* cmd = pool.acquire<SetRateCommand>();
     *
     * if (driver.sendCommandAsync(cmd)) {
     *     // Success - command queued
     * } else {
     *     // Failed - queue full
     * }
     *
     * // Always release - success or failure
     * cmd->release();
     * @endcode
     *
     * @warning Caller MUST call ctx->release() after function returns
     * @note Context automatically returned to pool after worker completes
     */
    bool sendCommandAsync(Command::Context* ctx);

    /**
     * @brief Send command and wait for response
     * @param ctx Command context (acquired from pool, owned by caller)
     * @retval true Command succeeded, response data available in ctx
     * @retval false Timeout or error occurred
     *
     * @par Ownership:
     * Caller ALWAYS retains ownership and MUST call ctx->release() after
     * this function returns, regardless of success or failure.
     *
     * @code
     * auto* cmd = pool.acquire<QueryVersionCommand>();
     *
     * if (driver.sendCommandSync(cmd, 2000)) {
     *     // Success - read response
     *     printf("Version: %s\n", cmd->version);
     * } else {
     *     // Timeout or error
     *     printf("Failed: %d\n", (int)cmd->getStatus());
     * }
     *
     * // Always release - success or failure
     * cmd->release();
     * @endcode
     *
     * @warning Caller MUST call ctx->release() after function returns
     * @note This function blocks until response received or timeout
     * @note Safe to read ctx after timeout - worker may still be processing
     */
    bool sendCommandSync(Command::Context* ctx);

private:

    /// Hardware resources
    Interface::IUart *mpUart = nullptr;
    Interface::IGpo *mpPwr = nullptr;

    // Callbacks
    RawDataCallback *mRawDataCallback = nullptr;
    AidingCallback *mAidingCallback = nullptr;
    NmeaCallback *mNmeaCallback = nullptr;

    /// Indicates that the driver is configured
    bool mIsConfigured = false;

    /// Indicates that the driver is initialized
    enum class InitState { None, InProgress, Initialized };
    std::atomic<InitState> mState{InitState::None};

    /// UART baudrate
    static constexpr uint32_t skUartBaudrate = 921600;
    /// Storage the uart wrapper keeps its receive stream in. initHw() hands
    /// it over and the wrapper owns it from then on, reading and writing it
    /// as its ring. Sized for the bulk reads the firmware updater takes in
    /// one go, not for NMEA.
    static constexpr size_t skUartRxBuffLength = 10*1024;
    uint8_t mUartRxBuffer[skUartRxBuffLength];

    /// One NMEA line under assembly: everything printable that arrives until a
    /// newline. A sentence stays far below this, and the headroom is for the
    /// proprietary $PAIR replies, whose length the standard does not cap and
    /// the module decides.
    static constexpr size_t skDataBuffLength = 1024;
    uint8_t mDataBuffer[skDataBuffLength];
    /// Bytes in data buffer
    size_t mDataBufferIdx = 0;

    /// Asks rxThread to drop the line it is holding. Honoured by rxThread
    /// itself, which is the only writer of the assembly state.
    std::atomic<bool> mParserReset {false};

    static constexpr size_t skTxBuffLength = 1024;
    uint8_t mTxBuffer[skTxBuffLength];

    /// Worker command queue
    OS::Queue<Command::Context*, 50> mWorkerQueue;
    /// Worker thread
    OS::Thread<AirohaBase, 4*1024> mWorkerThread;
    /// RX thread
    OS::Thread<AirohaBase, 4*1024> mRxThread;


    static constexpr uint32_t skUartTxTimeout = 100;
    static constexpr uint32_t skAckTimeout = 1000;  // Including processing time

    std::function<bool(std::string_view*, size_t)> mActiveCmdHandler;
    /// Guards mActiveCmdHandler. The worker thread assigns/clears it (and owns the
    /// stack-local ctx it captures), while the RX thread reads and invokes it.
    /// Held across the invocation so the worker cannot tear down the handler or its
    /// captured ctx while the RX thread is inside the call.
    OS::Mutex mCmdHandlerMutex;

    struct Flag {
          static constexpr uint32_t PowerOnAck      = (1 << 3);
          static constexpr uint32_t StartupGNSS     = (1 << 4);
    };

    OS::EventFlags mFlags;

    void workerThread();
    void initHw();
    void deinitHw();
    bool initRxThread();
    void deinitRxThread();
    void rxThread();
    void rxLineHandler(const char *line);
    bool nmeaHandler(std::string_view* tokens, size_t count);
    bool pairHandler(std::string_view* tokens, size_t count);

    void commandHandler(Command::Context *ctx);
    void intCmdHandler(Command::Context *ctx);
    void reqAiding(const ReqAiding& cmd);
    void applyEpo(const EpoData& epo, SystemType sys);
    void applyTime(const std::tm& utc);
    void applyLocation(const ReferenceLocation& loc);

    // isAck/isCmd are invoked from the active command handler on the RX thread
    // while mCmdHandlerMutex is held. Keep them pure parsing - they must not send
    // commands (sendRaw/sendGeneric), or the worker thread would deadlock.
    bool isAck(std::string_view* tokens, size_t count, std::string_view cmdId, Command::Status &status);
    bool isCmd(std::string_view token0, std::string_view cmdId);
    size_t makeCommand(char *buff, size_t buffSize, std::string_view cmdId, std::string_view payload);
    bool sendCommand(std::string_view cmd);

    // WARNING: the `rsp` callback runs on the RX thread (via mActiveCmdHandler)
    // while mCmdHandlerMutex is held. It MUST NOT call sendRaw()/sendGeneric()
    // (directly or indirectly): those re-acquire mCmdHandlerMutex on the worker
    // thread, and a synchronous send from within `rsp` would deadlock. Keep `rsp`
    // to pure parsing of the response tokens (see parseResponsePayload).
    Command::Status sendRaw(std::string_view cmdId, std::string_view raw,
            std::function<void(std::string_view*, size_t)> rsp,
            uint32_t timeout);

    // WARNING: see sendRaw() - `rsp` runs under mCmdHandlerMutex on the RX thread
    // and must not send commands (sendRaw/sendGeneric) or it will deadlock.
    Command::Status sendGeneric(std::string_view cmdId, std::string_view payload = "",
            std::function<void(std::string_view*, size_t)> rsp = nullptr,
            uint32_t timeout = skAckTimeout);

    Command::Status sendGeneric(Command::Context *ctx);

    bool ParseReqAiding(std::string_view* tokens, size_t count, ReqAiding &data);
    bool ParseIndicationSystemMessage(std::string_view* tokens, size_t count, IndicationSystemMessage &data);
};

} // namespace Airoha
