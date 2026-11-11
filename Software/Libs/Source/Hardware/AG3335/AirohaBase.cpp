/**
 ******************************************************************************
 * @file    AirohaBase.cpp
 * @brief   Base class for Airoha GPS.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "AG3335/AirohaBase.hpp"
#include "AG3335/AirohaCommandRegistry.hpp"
#include "AG3335/AirohaCommandGeneric.hpp"

#define LOG_MODULE_PRX      "Airoha.Base"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>
#include <cctype>
#include <cstring>

namespace Airoha
{

AirohaBase::AirohaBase()
{
    mFlags.init("Airoha.Flags");
    mWorkerQueue.init("Airoha.Queue");
    mCmdHandlerMutex.init("Airoha.CmdHandler");
}

AirohaBase::AirohaBase(Interface::IUart &uart, Interface::IGpo &pwr)
{
    config(uart, pwr);
    mFlags.init("Airoha.Flags");
    mWorkerQueue.init("Airoha.Queue");
    mCmdHandlerMutex.init("Airoha.CmdHandler");
}

AirohaBase::~AirohaBase()
{
    if(isInitialized()) {
        deinit();
    }
    mWorkerQueue.deinit();
    mFlags.deinit();
}

bool AirohaBase::config(Interface::IUart &uart, Interface::IGpo &pwr)
{
    mpUart = &uart;
    mpPwr = &pwr;

    mIsConfigured = true;

    return true;
}

bool AirohaBase::init()
{
    LOG_INFO("Initialization...\n");

    if (!mIsConfigured) {
        LOG_ERROR("Invalid state!\n");
        return false;
    }

    InitState expected = InitState::None;
    if (!mState.compare_exchange_strong(expected, InitState::InProgress)) {
        LOG_WARNING("Already initializing or initialized. State %u\n", expected);
        return false;
    }

    if (!mWorkerThread.init("Airoha.Worker", this, &AirohaBase::workerThread, osPriorityNormal, 5000)) {
        LOG_ERROR("mWorkerThread.init timeout\n");
        mState.store(InitState::None);
        return false;
    }

    mState.store(InitState::Initialized);
    LOG_DEBUG("Done\n");
    return true;
}

bool AirohaBase::deinit()
{
    LOG_INFO("Deinitialization...\n");

    if (!mIsConfigured) {
        LOG_ERROR("Invalid state!\n");
        return false;
    }

    InitState expected = InitState::Initialized;
    if (!mState.compare_exchange_strong(expected, InitState::None)) {
        LOG_WARNING("Not initialized or already deinitialized. State %u\n", expected);
        return true;
    }

    mWorkerThread.requestStop();
    if(!mWorkerThread.join(1000)) {
        LOG_ERROR("mWorkerThread.join timeout\n");
    }
    mWorkerThread.deinit();

    LOG_DEBUG("Done\n");
    return true;
}

bool AirohaBase::isInitialized() const
{
    return mState.load() == InitState::Initialized;
}

bool AirohaBase::powerOn(uint32_t timeout)
{
    bool status = false;
    auto *cmd = mContextPool.acquire<Command::Internal>();
    if (!cmd) {
        return false;
    }

    cmd->mId = Command::Internal::Id::ON;

    const uint32_t ackTimeout = 1000;
    cmd->setTimeout(std::max(ackTimeout, timeout));

    if (timeout > 0) {
        status = sendCommandSync(cmd);
    } else {
        status = sendCommandAsync(cmd);
    }

    cmd->release();

    return status;
}

bool AirohaBase::powerOff(uint32_t timeout)
{
    bool status = false;
    auto *cmd = mContextPool.acquire<Command::Internal>(1000);
    if (!cmd) {
        return false;
    }

    cmd->mId = Command::Internal::Id::OFF;

    const uint32_t ackTimeout = 1000;
    cmd->setTimeout(std::max(ackTimeout, timeout));

    if (timeout > 0) {
        status = sendCommandSync(cmd);
    } else {
        status = sendCommandAsync(cmd);
    }

    cmd->release();

    return status;
}

bool AirohaBase::isPowerOn()
{
    return mpPwr->get();
}

bool AirohaBase::waitForStartupGNSS(uint32_t timeout)
{
    return mFlags.waitAnyNoClear(Flag::StartupGNSS, timeout);
}

bool AirohaBase::cmdGeneric(std::string_view cmdId, uint32_t timeout, bool blocking)
{
    bool status = false;

    Command::Generic *cmd = nullptr;
    cmd = mContextPool.acquire<Command::Generic>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setTimeout(timeout);

    if (blocking) {
        status = sendCommandSync(cmd);
    } else {
        status = sendCommandAsync(cmd); // Use default timeut in Command::Context
    }

    cmd->release();

    return status;
}

bool AirohaBase::cmdGenericSetBOOL(std::string_view cmdId, bool v, uint32_t timeout, bool blocking)
{
    bool status = false;

    Command::GenericSetBOOL *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GenericSetBOOL>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setValue(v);
    cmd->setTimeout(timeout);

    if (blocking) {
        status = sendCommandSync(cmd);
    } else {
        status = sendCommandAsync(cmd);
    }

    cmd->release();

    return status;
}

bool AirohaBase::cmdGenericGetBOOL(std::string_view cmdId, bool &v, uint32_t timeout)
{
    bool status = false;

    Command::GenericGetBOOL *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GenericGetBOOL>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setHasResponse();
    cmd->setTimeout(timeout);

    status = sendCommandSync(cmd);
    if (status) {
        v = cmd->getValue();
    }

    cmd->release();

    return status;
}

bool AirohaBase::cmdGenericSetUINT(std::string_view cmdId, uint32_t v, uint32_t timeout, bool blocking)
{
    bool status = false;

    Command::GenericSetUINT *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GenericSetUINT>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setValue(v);
    cmd->setTimeout(timeout);

    if (blocking) {
        status = sendCommandSync(cmd);
    } else {
        status = sendCommandAsync(cmd);
    }

    cmd->release();

    return status;
}

bool AirohaBase::cmdGenericGetUINT(std::string_view cmdId, uint32_t &v, uint32_t timeout)
{
    bool status = false;

    Command::GenericGetUINT *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GenericGetUINT>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setHasResponse();
    cmd->setTimeout(timeout);

    status = sendCommandSync(cmd);
    if (status) {
        v = cmd->getValue();
    }

    cmd->release();

    return status;
}

bool AirohaBase::cmdGenericSetINT(std::string_view cmdId, int32_t v, uint32_t timeout, bool blocking)
{
    bool status = false;

    Command::GenericSetINT *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GenericSetINT>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setValue(v);
    cmd->setTimeout(timeout);

    if (blocking) {
        status = sendCommandSync(cmd);
    } else {
        status = sendCommandAsync(cmd);
    }

    cmd->release();

    return status;
}

bool AirohaBase::cmdGenericGetINT(std::string_view cmdId, int32_t &v, uint32_t timeout)
{
    bool status = false;

    Command::GenericGetINT *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GenericGetINT>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setHasResponse();
    cmd->setTimeout(timeout);

    status = sendCommandSync(cmd);
    if (status) {
        v = cmd->getValue();
    }

    cmd->release();

    return status;
}

bool AirohaBase::cmdGenericSetFLOAT(std::string_view cmdId, float v, uint8_t presigion, uint32_t timeout, bool blocking)
{
    bool status = false;

    Command::GenericSetFLOAT *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GenericSetFLOAT>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setValue(v);
    cmd->setTimeout(timeout);

    if (blocking) {
        status = sendCommandSync(cmd);
    } else {
        status = sendCommandAsync(cmd);
    }

    cmd->release();

    return status;
}

bool AirohaBase::cmdGenericGetFLOAT(std::string_view cmdId, float &v, uint32_t timeout)
{
    bool status = false;

    Command::GenericGetFLOAT *cmd = nullptr;
    cmd = mContextPool.acquire<Command::GenericGetFLOAT>();
    if (!cmd) {
        return false;
    }

    cmd->setCmdId(cmdId);
    cmd->setHasResponse();
    cmd->setTimeout(timeout);

    status = sendCommandSync(cmd);
    if (status) {
        v = cmd->getValue();
    }

    cmd->release();

    return status;
}

bool AirohaBase::sendCommandAsync(Command::Context* ctx)
{
    if (!isInitialized()) {
        return false;
    }

    if (ctx == nullptr) {
        return false;
    }

    // Mark as async (no completion semaphore)
    ctx->mCompletionToken = nullptr;

    // Add reference for the queue/worker
    // Current: refCount = 1 (caller owns)
    ctx->addRef();
    // New: refCount = 2 (caller + worker)

    // Try to enqueue
    if (!mWorkerQueue.push(ctx, 10)) {
        // Queue full - remove worker reference
        ctx->release();
        // Back to: refCount = 1 (caller still owns)
        return false;
        // Caller will release their reference
    }

    // Successfully queued
    // refCount = 2 (caller + worker)
    return true;
    // Caller must release their reference
    // Worker will release after processing
}

bool AirohaBase::sendCommandSync(Command::Context* ctx)
{
    if (!isInitialized()) {
        return false;
    }

    if (ctx == nullptr) {
        return false;
    }

    // Create binary semaphore for blocking
    osSemaphoreAttr_t semAttr {};
    semAttr.name = "CmdCompletion";

    ctx->mCompletionToken = osSemaphoreNew(1, 0, &semAttr);
    if (ctx->mCompletionToken == nullptr) {
        return false;
    }

    ctx->mCompleted = false;

    // Add reference for the worker
    // Current: refCount = 1 (caller owns)
    ctx->addRef();
    // New: refCount = 2 (caller + worker)

    // Send to queue
    if (!mWorkerQueue.push(ctx, 10)) {
        // Queue full - remove worker reference
        osSemaphoreDelete(ctx->mCompletionToken);
        ctx->mCompletionToken = nullptr;
        ctx->release();
        // Back to: refCount = 1 (caller owns)
        return false;
        // Caller will release their reference
    }

    // Wait for completion with timeout
    // refCount = 2 (caller + worker)
    osStatus_t status = osSemaphoreAcquire(ctx->mCompletionToken, ctx->getTimeout());

    if (status == osErrorTimeout) {
        ctx->setStatus(Command::Status::IntTimeout); // Overwrite status
    }

    bool success = (status == osOK) && ctx->isSuccess();

    // refCount still = 2 (caller + worker both have references)
    // Worker already released its reference when signaling semaphore
    // Actually refCount = 1 now (only caller)
    return success;
    // Caller must release their reference
    // After caller release -> refCount = 0 -> back to pool
}




void AirohaBase::workerThread()
{
    if (!initRxThread()) {
        LOG_ERROR("Failed to start RX thread\n");
        mWorkerThread.setInitFailed();
        return;
    }

    LOG_INFO("Initialized\n");

    // Initialization successful
    mWorkerThread.setInitialized();

    // Register Queue for automatic interruption
    OS::CancellableGuard guard(&mWorkerThread, &mWorkerQueue);

    //mWorkerThread.registerCancellable(&mWorkerQueue);

    // Main loop
    while (!mWorkerThread.shouldStop()) {

        // Wait for command (blocks until available)
        Command::Context *ctx = nullptr;
        mWorkerQueue.pop(ctx);

        if (mWorkerThread.shouldStop()) {
            LOG_DEBUG("Interrupted\n");
            if (ctx) {
                if (ctx->mCompletionToken != nullptr) {
                    ctx->mCompleted = true;
                    osSemaphoreRelease(ctx->mCompletionToken);
                }
                ctx->setStatus(Command::Status::IntError);
                ctx->release();
            }
            break;
        }

        if (ctx == nullptr) {
            LOG_ERROR("[%s] ctx == nullptr\n");
            continue;
        }


        // Ignore all commands after power off except powerOn
        auto isAllowed = [&](Command::Context *c) -> bool
        {
            if (isPowerOn()) {
                return true;
            } else {
                Command::Internal *cmd = reinterpret_cast<Command::Internal*>(c);
                return cmd->mId == Command::Internal::Id::ON;
            }
        };

        if (!isAllowed(ctx)) {
            LOG_DEBUG("Ignore command '%.*s' after power off\n",
                    ctx->getCmdId().size(), ctx->getCmdId().data());
            ctx->release();
            continue;
        }


        // Process command
        if (ctx->getCmdId() == Command::Internal::skCmdId) {
            intCmdHandler(ctx);
        } else {
            commandHandler(ctx);
        }

        // If this was a blocking command, signal completion
        if (ctx->mCompletionToken != nullptr) {
            ctx->mCompleted = true;
            osSemaphoreRelease(ctx->mCompletionToken);
        }

        // Worker releases its reference
        // If initiator already released (async or timeout), context returns to pool
        ctx->release();
    }

    deinitRxThread();

    // Clear Queue. Context must be returned to the pool
    Command::Context *ctxLeft = nullptr;
    while (mWorkerQueue.pop(ctxLeft, 0)) {
        if (ctxLeft) {
            LOG_DEBUG("Clear pending command '%.*s'\n",
                    ctxLeft->getCmdId().size(), ctxLeft->getCmdId().data());
            if (ctxLeft->mCompletionToken != nullptr) {
                ctxLeft->mCompleted = true;
                osSemaphoreRelease(ctxLeft->mCompletionToken);
            }
            ctxLeft->setStatus(Command::Status::IntError);
            ctxLeft->release();
        }
    }

    LOG_INFO("Deinitialized\n");
}

void AirohaBase::initHw()
{
    mpPwr->init(Interface::IGpo::Type::PP, false, false);
    mpUart->init();
    mpUart->setBuffer(mUartRxBuffer, sizeof(mUartRxBuffer));
    mpUart->setBaudrate(skUartBaudrate);
}

void AirohaBase::deinitHw()
{
    mpUart->stopReception();
    mpUart->deinit();
    mpPwr->set(false);
    mpPwr->deinit();
}

bool AirohaBase::initRxThread()
{
    initHw();

    if(!mRxThread.init("Airoha.RX", this, &AirohaBase::rxThread, osPriorityNormal, 5000)) {
        LOG_ERROR("mRxThread.init timeout\n");
        deinitHw();
        return false;
    }

    return true;
}

void AirohaBase::deinitRxThread()
{
    mRxThread.requestStop();
    mpUart->abortRx(); // unblock RxThread
    if(!mRxThread.join(1000)) {
        LOG_ERROR("mWorkerThread.join timeout\n");
    }
    mRxThread.deinit();

    deinitHw();
}

void AirohaBase::rxThread()
{
    LOG_DEBUG("Started\n");

    uint8_t binBuffer[128] {};
    size_t binSize = 0;

    // Initialization successful
    mRxThread.setInitialized();

    // Reset buffer
    mDataBufferIdx = 0;
    mDataBuffer[mDataBufferIdx] = '\0';

    // Main loop
    while (!mRxThread.shouldStop()) {

        // Check buffer overflow. 1 byte for null-terminator
        if (mDataBufferIdx >= (skDataBuffLength - 1)) {
            // Input buffer is full. Discard it.
            mDataBufferIdx = 0;
        }

        size_t br = 0;
        uint8_t byte = 0;

        // The read parks until a byte arrives, and the assembly state can be
        // reset meanwhile, so nothing handed to it may point into that state.
        bool rxSomething = mpUart->receive(&byte, 1, br);

        if (mRxThread.shouldStop()) {
            LOG_DEBUG("Interrupted\n");
            break;  // Terminate thread after unblocking
        }

        if (!rxSomething || br == 0) {
            continue;   // Nothing to process
        }

        // Between the read returning and the byte being stored, so the byte
        // that opens a session is kept and lands at the start of the buffer.
        if (mParserReset.exchange(false)) {
            mDataBufferIdx = 0;
        }

        mDataBuffer[mDataBufferIdx] = byte;

        // Binary data
        if (mRawDataCallback) {
            binBuffer[binSize++] = byte;
            if (binSize == sizeof(binBuffer)) {
                mRawDataCallback->onRawDataBin(binBuffer, binSize);
                binSize = 0;
            }
        }

        // Skip garbage
        auto isNmeaChar = [](unsigned char c)
        {
            if (c == '\r' || c == '\n') return true;
            return (c >= 0x20 && c <= 0x7E);    // printable ASCII
        };

        // If we get garbage in the middle of the term,
        // there is no point in parsing it, we wait for
        // the next one
        if (!isNmeaChar(byte)) {
            mDataBufferIdx = 0;
            mDataBuffer[mDataBufferIdx] = '\0';
            continue;
        }

        mDataBufferIdx++;
        mDataBuffer[mDataBufferIdx] = '\0';   // make sure that buffer is null-terminated

        // Looks like garbage. Reset buffer
        // $PAIR1$PAIR001,165,0*39
        if (mDataBuffer[mDataBufferIdx - 1] == '$' && mDataBufferIdx != 1) {
            mDataBuffer[0] = '$';
            mDataBufferIdx = 1;
            mDataBuffer[mDataBufferIdx] = '\0';
        }

        // Check for '\n'
        if (mDataBuffer[mDataBufferIdx - 1] != '\n') {
            continue;
        }


        if (mRawDataCallback) {
            mRawDataCallback->onRawDataRx({reinterpret_cast<const char*>(mDataBuffer), mDataBufferIdx});
        }

        // Remove '\n'
        mDataBufferIdx--;
        mDataBuffer[mDataBufferIdx] = '\0';

        // Remove '\r'
        if (mDataBufferIdx > 0 && mDataBuffer[mDataBufferIdx - 1] == '\r') {
            mDataBufferIdx--;
            mDataBuffer[mDataBufferIdx] = '\0';
        }

        if (mDataBufferIdx == 0) {
            continue; // Skip empty lines
        }

        // Process line
        rxLineHandler(reinterpret_cast<const char*>(mDataBuffer));

        // Clear buffer
        mDataBufferIdx = 0;
        mDataBuffer[mDataBufferIdx] = '\0';
    }

    // Flush binary data
    // Binary data
    if (mRawDataCallback) {
        if (binSize > 0) {
            mRawDataCallback->onRawDataBin(binBuffer, binSize);
            binSize = 0;
        }
    }

    LOG_DEBUG("Stopped\n");
}

void AirohaBase::rxLineHandler(const char *line)
{
    LOG_DEBUG("RX: [%s]\n", line);

    const size_t kMaxTokens = 50;
    static std::string_view tokens[kMaxTokens];
    size_t tokensCount = Nmea::TokenizeFull(line, tokens, kMaxTokens);

    if (tokensCount == 0) {
        return;
    }

    if (mRawDataCallback) {
        mRawDataCallback->onRawTokensRx(tokens, tokensCount);
    }

    auto startsWith = [](std::string_view sv, std::string_view prefix)
    {
        return sv.size() >= prefix.size() &&
                sv.substr(0, prefix.size()) == prefix;
    };

    if (startsWith(tokens[0], "PAIR")) {
        //LOG_DEBUG("RX: [%s]\n", line);
        pairHandler(tokens, tokensCount);
    } else {
        //LOG_DEBUG("RX: [%s]\n", line);
        nmeaHandler(tokens, tokensCount);
    }

}

bool AirohaBase::nmeaHandler(std::string_view* tokens, size_t count)
{
    Nmea::NmeaData nmeaData {};

    if (!Nmea::ParseNmea(tokens, count, nmeaData)) {
//        LOG_DEBUG("Failed to parse NMEA sentence [%.*s]\n",
//                        tokens[0].size(), tokens[0].data());
        return false;
    }

    switch (nmeaData.type) {
        case Nmea::NmeaSentenceType::GPGGA:
        case Nmea::NmeaSentenceType::GNGGA:
        case Nmea::NmeaSentenceType::GLGGA:
        case Nmea::NmeaSentenceType::GAGGA:
        case Nmea::NmeaSentenceType::BDGGA: {
            // Process GGA data
            auto *cmd = mContextPool.acquire<Command::Internal>();
            if (cmd){
                cmd->mId = Command::Internal::Id::NMEA_GGA;
                cmd->nmeaGGA = nmeaData.data.gga;
                sendCommandAsync(cmd);
                cmd->release();
            } else {
                LOG_ERROR("No memory for GGA processing\n");
            }
        } break;

        case Nmea::NmeaSentenceType::GPRMC:
        case Nmea::NmeaSentenceType::GNRMC:
        case Nmea::NmeaSentenceType::GLRMC:
        case Nmea::NmeaSentenceType::GARMC:
        case Nmea::NmeaSentenceType::BDRMC: {
            // Process RMC data
            auto *cmd = mContextPool.acquire<Command::Internal>();
            if (cmd){
                cmd->mId = Command::Internal::Id::NMEA_RMC;
                cmd->nmeaRMC = nmeaData.data.rmc;
                sendCommandAsync(cmd);
                cmd->release();
            } else {
                LOG_ERROR("No memory for RMC processing\n");
            }
        } break;

        case Nmea::NmeaSentenceType::GPGSV:
        case Nmea::NmeaSentenceType::GNGSV:
        case Nmea::NmeaSentenceType::GLGSV:
        case Nmea::NmeaSentenceType::GAGSV:
        case Nmea::NmeaSentenceType::GBGSV: {
            // Process GSV data
            if (nmeaData.data.gsv.total_satellites > 0) {   // Skip empty strings
                auto *cmd = mContextPool.acquire<Command::Internal>();
                if (cmd){
                    cmd->mId = Command::Internal::Id::NMEA_GSV;
                    cmd->nmeaGSV = nmeaData.data.gsv;
                    sendCommandAsync(cmd);
                    cmd->release();
                } else {
                    LOG_ERROR("No memory for GSV processing\n");
                }
            }
        } break;

        case Nmea::NmeaSentenceType::UNKNOWN:
        default:
            LOG_DEBUG("Unsupported sentence type [%.*s]\n",
                    tokens[0].size(), tokens[0].data());
            return false;
    }

    return true;
}

bool AirohaBase::pairHandler(std::string_view* tokens, size_t count)
{
    // Process PAIR ACK or response.
    // Hold mCmdHandlerMutex across the test-and-invoke: the worker thread owns
    // mActiveCmdHandler and the stack-local ctx it captures, and may clear/reassign
    // it from another thread. Locking here prevents reading a torn std::function and
    // keeps ctx alive until this invocation returns.
    {
        OS::MutexCS cs(mCmdHandlerMutex);
        if (mActiveCmdHandler) {
            if (mActiveCmdHandler(tokens, count)) {
                return true; // The command is processed, nothing more to do
            }
        }
    }

    // Process PAIR unsolicited commands

    // PAIR_GNSS_SUBSYS_POWER_ON
    // Waiting for ACK ($PAIR001,002,0*39)
    Command::Status status;
    if (isAck(tokens, count, "002", status) && status == Command::Status::Success) {
        mFlags.set(Flag::PowerOnAck);
    }

    // PAIR_REQUEST_AIDING
    if (isCmd(tokens[0], "010") && mFlags.isSet(Flag::PowerOnAck)) {
        ReqAiding data {};
        if (ParseReqAiding(tokens, count, data)) {
            auto *cmd = mContextPool.acquire<Command::Internal>();
            if (cmd){
                cmd->mId = Command::Internal::Id::PAIR_010;
                cmd->pair010 = data;
                sendCommandAsync(cmd);
                cmd->release();
            } else {
                LOG_ERROR("No memory for PAIR_REQUEST_AIDING processing\n");
            }
        }
        return true;
    }

    // PAIR_INDICATION_SYSTEM_MESSAGE
    if (isCmd(tokens[0], "011")) {
        IndicationSystemMessage data {};
        if (ParseIndicationSystemMessage(tokens, count, data)) {
            if (data.type == IndicationSystemMessage::Type::STARTUP) {
                mFlags.set(Flag::StartupGNSS);
            }
        }
        return true;
    }

    return false;
}

void AirohaBase::commandHandler(Command::Context *ctx)
{
    if (ctx->getCmdId() == "002" || // PAIR_GNSS_SUBSYS_POWER_ON
        ctx->getCmdId() == "003" || // PAIR_GNSS_SUBSYS_POWER_OFF
        ctx->getCmdId() == "004" || // PAIR_GNSS_SUBSYS_HOT_START
        ctx->getCmdId() == "005" || // PAIR_GNSS_SUBSYS_WARM_START
        ctx->getCmdId() == "006" || // PAIR_GNSS_SUBSYS_COLD_START
        ctx->getCmdId() == "023")   // PAIR_SYSTEM_REBOOT
    {
        mFlags.clear(Flag::StartupGNSS);
    }

    sendGeneric(ctx);
}

void AirohaBase::intCmdHandler(Command::Context *ctx)
{
    Command::Internal *cmd = reinterpret_cast<Command::Internal*>(ctx);

    switch(cmd->mId) {

        case Command::Internal::Id::ON:
            if (!mpPwr->get()) {
                mFlags.clear(Flag::StartupGNSS | Flag::PowerOnAck);
                mpPwr->set(true);

                // The assembler is cleared with the transport: a line left half
                // built belongs to the session that ended.
                mParserReset = true;
                mpUart->flushRx();
                mpUart->startReception();

                // Better to wait for ACK ($PAIR001,002,0*39)
                // before sending other commands.
                if (mFlags.waitAnyNoClear(Flag::PowerOnAck, cmd->getTimeout())) {
                    cmd->setStatus(Command::Status::Success);
                    LOG_DEBUG("Power ON\n");
                } else {
                    // Fallback
                    mpUart->stopReception();
                    mpPwr->set(false);
                    cmd->setStatus(Command::Status::Failed);
                    LOG_DEBUG("Power ON timeout\n");
                }

            } else {
                LOG_WARNING("Power is already ON\n");
                cmd->setStatus(Command::Status::Success);
            }
            break;

        case Command::Internal::Id::OFF:
            if (mpPwr->get()) {
                mpUart->stopReception();
                mpPwr->set(false);
                mFlags.clear(Flag::StartupGNSS | Flag::PowerOnAck);
                LOG_DEBUG("Power OFF\n");
            } else {
                LOG_WARNING("Power is already OFF\n");
            }
            cmd->setStatus(Command::Status::Success);
            break;

        case Command::Internal::Id::NMEA_GGA:
            if (mNmeaCallback) {
                mNmeaCallback->onNmeaGGA(cmd->nmeaGGA);
            }
            if (mAidingCallback && cmd->nmeaGGA.fix_quality == Nmea::GGA::GPS_FIX) {
                mAidingCallback->onAidingRefLocationSave(cmd->nmeaGGA.latitude,
                        cmd->nmeaGGA.longitude, cmd->nmeaGGA.altitude);
             }
            break;

        case Command::Internal::Id::NMEA_RMC:
            if (mNmeaCallback) {
                mNmeaCallback->onNmeaRMC(cmd->nmeaRMC);
            }
            break;

        case Command::Internal::Id::NMEA_GSV:
            if (mNmeaCallback) {
                mNmeaCallback->onNmeaGSV(cmd->nmeaGSV);
            }
            break;

        case Command::Internal::Id::PAIR_010:
            reqAiding(cmd->pair010);
            break;

        case Command::Internal::Id::NONE:
        default:
            break;
    }
}

void AirohaBase::reqAiding(const ReqAiding& cmd)
{
    if (!mAidingCallback) {
        return;
    }

    switch(cmd.request) {
        case ReqAiding::EPO: {
            LOG_DEBUG("ReqAiding: EPO. System %u, week %u, tow %u\n",
                    cmd.system, cmd.week, cmd.tow);
            EpoData epo = mAidingCallback->onAidingEpoRequest(static_cast<SystemType>(cmd.system), cmd.week, cmd.tow);
            if (!epo.empty()){
                applyEpo(epo, static_cast<SystemType>(cmd.system));
                LOG_DEBUG("ReqAiding: EPO. System %u. DONE\n", cmd.system);
            }
        } break;

        case ReqAiding::TIME: {
            LOG_DEBUG("ReqAiding: TIME\n");
            std::tm utc {};
            if (mAidingCallback->onAidingRefTimeRequest(utc)) {
                applyTime(utc);
                LOG_DEBUG("ReqAiding: TIME. DONE\n");
            }
        } break;

        case ReqAiding::LOCATION: {
            LOG_DEBUG("ReqAiding: LOCATION\n");
            ReferenceLocation loc {};
            if (mAidingCallback->onAidingRefLocationRequest(loc)) {
                applyLocation(loc);
                LOG_DEBUG("ReqAiding: LOCATION. DONE\n");
            }
        } break;

        case ReqAiding::IONOSPHERIC_MAP: {
            LOG_WARNING("ReqAiding: Not implemented!. IONOSPHERIC_MAP. System %u, week %u, tow %u, lat %u, lon %u\n",
                                cmd.system, cmd.week, cmd.tow, cmd.lat, cmd.lon);
        } break;

        default:
            break;
    }
}

void AirohaBase::applyEpo(const EpoData& epo, SystemType sys)
{
    // The segments arrive already chosen for this system and the current
    // window (EpoParser::selectSegments), so there is nothing left to filter.
    const size_t segments = epo.segmentCount();
    if (segments == 0) {
        return;
    }

    auto rsp = [](std::string_view* tokens, size_t count) { };

    const size_t payloadSize = 256;
    char payload[payloadSize];

    auto fillPayload = [&](uint8_t prn, const uint8_t* data) {
        // PAIR471 format: $PAIR471,<System_ID>,<SV_ID>,<W[0]~W[17]>*CS<CR><LF>
        // W[0] ~ W[17]: words [LSB first] of one EPO segment data (total 72 bytes)
        size_t pos = 0;
        pos += snprintf(payload + pos, payloadSize - pos,"%u,%X",
                            static_cast<uint8_t>(sys), prn);

        for (size_t i = 0; i < 18; i++) { // 18 = 72/4
            // memcpy, not a uint32_t load: nothing here guarantees @p data is
            // word-aligned, and a misaligned LDRD/LDM faults on Cortex-M.
            uint32_t v = 0;
            std::memcpy(&v, &data[i*4], sizeof(v));
            pos += snprintf(payload + pos, payloadSize - pos, ",%lX", v);
        }
    };


    // lock sleep
    if (sendGeneric("382", "1") != Command::Status::Success) {
        return;
    }

    // Galileo or Beidou needs to send 72 bytes header.
    if (epo.hasHeader) {
        fillPayload(sys == SystemType::GALILEO ? 254 : 255, epo.header);
        sendGeneric("471", payload, rsp);
    }

    for (size_t i = 0; i < segments; i++) {
        const uint8_t *raw = epo.segments.data() + i * EpoData::kSegmentSize;

        // Byte 3 of a segment is its SV ID.
        fillPayload(EpoParser::svIdToPRN(raw[3]), raw);

        sendGeneric("471", payload, rsp);

#if 1
        // We are waiting for ACK and debug response, so no need additional delay
        //  Response:
        //      $PAIR001,471,0*39\r\n ==> Success
        //      $PAIR471,56056272*13\r\n
#else
        // Pace the segments instead of waiting for each acknowledgement
        OS::Delay(10);
#endif
    }

    // unlock sleep
    if (!mUseL1Only) {
        sendGeneric("382", "0");
    } else {
        //LOG_WARNING("Sleep disabled due L1 mode\n");
    }

#if 0 // Confirm
    OS::Delay(1000);
    snprintf(payload, payloadSize,"%u", static_cast<uint8_t>(sys));
    sendGeneric("470", payload, rsp);
#endif
}

void AirohaBase::applyTime(const std::tm& utc)
{
    // $PAIR590,<YYYY>,<MM>,<DD>,<hh>,<mm>,<ss>*CS<CR><LF>
    // $PAIR590,2019,2,10,9,0,58*0B\r\n
    char payload[32] {};

    snprintf(payload, sizeof(payload),
            "%04u,%02u,%02u,%02u,%02u,%02u",
            utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
            utc.tm_hour, utc.tm_min, utc.tm_sec);

    auto rsp = [](std::string_view* tokens, size_t count) { };

    sendGeneric("590", payload, rsp);
}

void AirohaBase::applyLocation(const ReferenceLocation& loc)
{
    // $PAIR600,<Latitude>,<Longitude>,<Height>,<AccMaj>,<AccMin>,<Bear>,<AccVert>*CS<CR><LF>
    // $PAIR600,45.000000,-30.000000,175.0,50.0,50.0,0,100.0*0D\r\n
    char payload[128] {};

    snprintf(payload, sizeof(payload),
                "%.6f,%.6f,%.1f,%.1f,%.1f,%.0f,%.1f",
                loc.latitude, loc.longitude, loc.height,
                loc.accMaj, loc.accMin, loc.bear, loc.accVert);

    auto rsp = [](std::string_view* tokens, size_t count) { };

    sendGeneric("600", payload, rsp);
}


bool AirohaBase::isAck(std::string_view* tokens, size_t count, std::string_view cmdId, Command::Status &status)
{
    // $PAIR001,<command ID>,<result>: the acknowledgement of a command.
    // The result codes map onto Command::Status below.

    // Tokens: PAIR001 Command_ID Result
    std::string_view ack = "PAIR001";
    if (count == 3 &&
        tokens[0] == ack &&
        tokens[1] == cmdId &&
        tokens[2].size() == 1)
    {
        switch (tokens[2].data()[0])
        {
            case '0': status = Command::Status::Success; break;
            case '1': status = Command::Status::Processing; break;
            case '2': status = Command::Status::Failed; break;
            case '3': status = Command::Status::CmdIdNotSupported; break;
            case '4': status = Command::Status::ParameterError; break;
            case '5': status = Command::Status::ServiceBusy; break;
            default:  status = Command::Status::IntParsingError; break;
        }

        return true;
    }

    return false;
}

bool AirohaBase::isCmd(std::string_view token0, std::string_view cmdId)
{
    return token0.size() == 4 + cmdId.size() &&
           token0.compare(4, cmdId.size(), cmdId) == 0;
}

size_t AirohaBase::makeCommand(char *buff, size_t buffSize, std::string_view cmdId, std::string_view payload)
{
    size_t length = 0;

    if (buff == nullptr || buffSize < 12 || cmdId.size() != 3) {
        return 0;
    }

    length += snprintf(&buff[length], buffSize - length, "$PAIR%.*s", cmdId.size(), cmdId.data());
    if (payload.size() > 0) {
        length += snprintf(&buff[length], buffSize - length, ",%.*s", payload.size(), payload.data());
    }
    uint8_t checksum = Nmea::CalcChecksum({&buff[1], length - 1});

    length += snprintf(&buff[length], buffSize - length, "*%02X\r\n", checksum);

    return length;
}

bool AirohaBase::sendCommand(std::string_view cmd)
{
    LOG_DEBUG("TX: [%.*s]\n", cmd.size() - 2, cmd.data()); // skip "\r\n" for pretty output

    if (mRawDataCallback) {
        mRawDataCallback->onRawDataTx(cmd);
    }
    return mpUart->transmit(reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size(), skUartTxTimeout);
}

Command::Status AirohaBase::sendRaw(std::string_view cmdId, std::string_view raw,
        std::function<void(std::string_view*, size_t)> rsp,
        uint32_t timeout)
{
    // Context for processing the command response.
    // Contains all variables used inside the handler lambda.
    // Capturing only one context pointer allows:
    // - to minimize the size of the closure,
    // - to avoid dynamic memory allocations in std::function,
    // - to keep all local variables on the function stack.
    struct Context {
        OS::EventFlags flags;
        uint32_t flagAck;
        uint32_t flagData;
        uint32_t flagMask;
        Command::Status* ack;
        const std::function<void(std::string_view*, size_t)> rsp;
        std::string_view cmdId;
        AirohaBase* self; // for isAck / isCmd
    };

    Command::Status ack {Command::Status::IntError};

    Context ctx{
        OS::EventFlags{"ACK"},
        (1u << 0),           // flagAck
        (2u << 0),           // flagData
        rsp ? ((1u << 0) | (2u << 0)) : (1u << 0),
        &ack,
        rsp,
        cmdId,
        this
    };

    // Handler with minimal closure.
    // NOTE: this runs on the RX thread (pairHandler) while mCmdHandlerMutex is held.
    // Everything it calls - c.self->isAck/isCmd and c.rsp - must stay re-entrancy
    // free and MUST NOT call sendRaw()/sendGeneric(), or the worker's matching
    // lock acquisition would deadlock. Keep it to parsing only.
    auto handler = [ctxPtr = &ctx](std::string_view* tokens, size_t count) -> bool
    {
        auto& c = *ctxPtr;

        // 1. Waiting ACK
        if ((c.flagMask & c.flagAck) && !c.flags.isSet(c.flagAck)) {
            if (c.self->isAck(tokens, count, c.cmdId, *c.ack)) {
                if (*c.ack != Command::Status::Processing) {
                    c.flags.set(c.flagAck);
                }
            }
        }

        // 2. Waiting data
        if ((c.flagMask & c.flagData) && !c.flags.isSet(c.flagData)) {
            if (c.self->isCmd(tokens[0], c.cmdId)) {
                if (c.rsp) {
                    (c.rsp)(tokens, count); // Call user handler
                }
                c.flags.set(c.flagData);
            }
        }

        return c.flags.get() == c.flagMask;
    };

    // Publish the handler under the lock so the RX thread never sees a torn
    // std::function. Release before waiting: the RX thread must be able to take
    // the lock to invoke the handler (which sets the flag we wait on below).
    {
        OS::MutexCS cs(mCmdHandlerMutex);
        mActiveCmdHandler = handler;
    }
//LOG_DEBUG("send %.*s, timeout %u\n", cmdId.size(), cmdId.data(), timeout);
    if (sendCommand(raw)) {
        if (!ctx.flags.wait(ctx.flagMask, osFlagsWaitAll, timeout)) {
            ack = Command::Status::IntTimeout;
        }
    } else {
        ack = Command::Status::IntError;
    }
//LOG_DEBUG("done. status %d\n", ack);
    // Clear under the lock. This blocks until any in-flight RX invocation has
    // finished, so ctx (destroyed when this function returns) is never accessed
    // after its scope ends.
    {
        OS::MutexCS cs(mCmdHandlerMutex);
        mActiveCmdHandler = nullptr;
    }

    return ack;
}

Command::Status AirohaBase::sendGeneric(std::string_view cmdId, std::string_view payload,
        std::function<void(std::string_view*, size_t)> rsp, uint32_t timeout)
{
    const size_t buffSize = skTxBuffLength;
    size_t length = 0;
    char *buff = reinterpret_cast<char*>(mTxBuffer);

    length = makeCommand(buff, buffSize, cmdId, payload);

    return sendRaw(cmdId, {buff, length}, rsp, timeout);
}

Command::Status AirohaBase::sendGeneric(Command::Context *ctx)
{
    const size_t buffSize = skTxBuffLength;
    size_t length = 0;
    char *buff = reinterpret_cast<char*>(mTxBuffer);

    length += snprintf(&buff[length], buffSize - length, "$PAIR%.*s,", ctx->getCmdId().size(), ctx->getCmdId().data());

    size_t payloadLength = ctx->formatCommandPayload(&buff[length], buffSize - length - 5);  // 5 bytes for checksum

    if (payloadLength == 0) {
        length--; // remove comma
    } else {
        length += payloadLength;
    }

    uint8_t checksum = Nmea::CalcChecksum({&buff[1], length - 1});

    length += snprintf(&buff[length], buffSize - length, "*%02X\r\n", checksum);


    Command::Status status = Command::Status::IntError;

    if (ctx->hasResponce()) {
        bool parsed = false;
        // Runs under mCmdHandlerMutex on the RX thread: parse only, never send a
        // command from here (would deadlock). parseResponsePayload is pure parsing.
        auto rsp = [&](std::string_view* tokens, size_t count) {
            parsed = ctx->parseResponsePayload(tokens, count);
        };

        status = sendRaw(ctx->getCmdId(),
                    {buff, length},
                    rsp,
                    ctx->getTimeout());
        if (status == Command::Status::Success && parsed == false) {
            status = Command::Status::IntParsingError;
        }
    } else {
        status = sendRaw(ctx->getCmdId(),
                    {buff, length},
                    nullptr,
                    ctx->getTimeout());
    }

    ctx->setStatus(status);
    return status;
}


bool AirohaBase::ParseReqAiding(std::string_view* tokens, size_t count,
                    ReqAiding &data)
{
    // Minimum: PAIR010, Type, GNSS_System
    // Type 1,2: PAIR010, Type, -1 (3 tokens)
    // Type 0: PAIR010, Type, GNSS_System, Week, ToW (5 tokens)
    // Type 6: PAIR010, Type, -1, Week, ToW, Lat, Lon (7 tokens)

    if (count < 3) {
        return false;
    }

    // Token 0 is "PAIR010" - already verified by caller

    // Parse Type (token 1)
    int type_val = -1;
    auto result = std::from_chars(tokens[1].data(),
                                  tokens[1].data() + tokens[1].size(),
                                  type_val);
    if (result.ec != std::errc() || type_val < 0) {
        return false;
    }

    switch (type_val) {
        case 0: data.request = ReqAiding::Type::EPO; break;
        case 1: data.request = ReqAiding::Type::TIME; break;
        case 2: data.request = ReqAiding::Type::LOCATION; break;
        case 6: data.request = ReqAiding::Type::IONOSPHERIC_MAP; break;
        default: return false;
    }

    // Parse GNSS_System (token 2)
    int system_val = -1;
    result = std::from_chars(tokens[2].data(),
                            tokens[2].data() + tokens[2].size(),
                            system_val);
    if (result.ec != std::errc()) {
        return false;
    }

    if (system_val == -1) {
        data.system = SystemType::UNKNOWN;
    } else if (system_val >= 0 && system_val <= 4) {
        data.system = static_cast<SystemType>(system_val);
    } else {
        data.system = SystemType::UNKNOWN;
    }

    // Parse based on request type
    if (data.request == ReqAiding::Type::EPO) {
        // Need: Week_Number, Time_of_Week (5 tokens total)
        if (count < 5) {
            return false;
        }

        result = std::from_chars(tokens[3].data(),
                                tokens[3].data() + tokens[3].size(),
                                data.week);
        if (result.ec != std::errc()) {
            return false;
        }

        result = std::from_chars(tokens[4].data(),
                                tokens[4].data() + tokens[4].size(),
                                data.tow);
        if (result.ec != std::errc()) {
            return false;
        }

    } else if (data.request == ReqAiding::Type::IONOSPHERIC_MAP) {
        // Need: Week, ToW, Lat, Lon (7 tokens total)
        if (count < 7) {
            return false;
        }

        result = std::from_chars(tokens[3].data(),
                                tokens[3].data() + tokens[3].size(),
                                data.week);
        if (result.ec != std::errc()) {
            return false;
        }

        result = std::from_chars(tokens[4].data(),
                                tokens[4].data() + tokens[4].size(),
                                data.tow);
        if (result.ec != std::errc()) {
            return false;
        }

        // Parse latitude (float)
        char* end_ptr = nullptr;
        data.lat = std::strtof(tokens[5].data(), &end_ptr);
        if (end_ptr == tokens[5].data()) {
            return false;
        }

        // Parse longitude (float)
        data.lon = std::strtof(tokens[6].data(), &end_ptr);
        if (end_ptr == tokens[6].data()) {
            return false;
        }

    } else if (data.request == ReqAiding::Type::TIME ||
               data.request == ReqAiding::Type::LOCATION) {
        // Only 3 tokens needed, GNSS_System should be -1
        if (system_val != -1) {
            return false;
        }
    }

    return true;
}

bool AirohaBase::ParseIndicationSystemMessage(std::string_view* tokens, size_t count, IndicationSystemMessage &data)
{
    // Token 0 is "PAIR011" - already verified by caller

    // Parse Type (token 1)
    if (count < 2 || tokens[1].size() > 1) {
        return false;
    }

    // Validate status (token 1)
    char mode_char = tokens[1][0];
    switch (mode_char) {
        case '1':
            data.type = IndicationSystemMessage::Type::STARTUP;
            break;
        default:
            return false;
    }

    return true;
}

} // namespace Airoha
