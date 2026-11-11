/**
 * @file DualAppComm.hpp
 * @brief Dual-part application communication (Service + GUI)
 * 
 * Manages communication for applications consisting of two parts:
 * - Service (background logic)
 * - GUI (user interface)
 * 
 * Architecture:
 * - 3 queues: Common (both->kernel), Service (kernel->srv), GUI (kernel->gui)
 * - Internal message routing between Service <-> GUI based on message type
 * - Single processing thread handles common queue
 * 
 * Internal message types (0x0000_8000 - 0x0000_8FFF):
 * - Used for direct Service <-> GUI communication
 * - Bypasses kernel processing
 * - Routes automatically based on type range
 */

#ifndef __APP_DUAL_APP_COMM_HPP
#define __APP_DUAL_APP_COMM_HPP

#include "SDK/Interfaces/IAppComm.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "App/LogThrottle.hpp"
#include "App/MessageManager.hpp"
#include "OS.hpp"

namespace App
{

/**
 * @brief Dual-part application communication manager
 * 
 * Manages Service + GUI as single application with:
 * - 2 receive queues (Service, GUI)
 * - 1 common send queue (both -> kernel)
 * - Internal routing (Service <-> GUI direct) based on message type
 * 
 * Internal message type range: 0x0000_8000 - 0x0000_8FFF
 * Messages in this range route directly between Service and GUI
 */
class DualAppComm {
public:
    // =========================================================================
    // Configuration constants
    // =========================================================================
    
    static constexpr size_t COMMON_QUEUE_SIZE = 40;   // Both parts -> kernel
    static constexpr size_t SERVICE_QUEUE_SIZE = 50;  // Kernel -> Service
    static constexpr size_t GUI_QUEUE_SIZE = 20;      // Kernel -> GUI
    
    static constexpr uint32_t SEND_TIMEOUT_MS = 10;   // Queue push timeout
    

    /**
     * @brief Construct dual application communication
     * @param servicePID Service process identifier
     * @param guiPID GUI process identifier
     * @param msgManager Reference to message manager
     * 
     * @note PIDs must be unique and non-zero
     * @note All queues initialized automatically
     */
    DualAppComm(uint32_t servicePID, 
                uint32_t guiPID, 
                App::MessageManager& msgManager);
    
    /**
     * @brief Destructor
     * @note Cleans up all queues
     */
    ~DualAppComm();
    
    App::MessageManager& getMsgManager()
    {
        return mMessageManager;
    }

    // =========================================================================
    // IAppComm interface access (for Service and GUI threads)
    // =========================================================================
    
    /**
     * @brief Get Service communication interface
     * @retval Pointer to IAppComm for Service thread
     */
    SDK::Interface::IAppComm* getServiceComm() { return &mServiceComm; }
    
    /**
     * @brief Get GUI communication interface
     * @retval Pointer to IAppComm for GUI thread
     */
    SDK::Interface::IAppComm* getGuiComm() { return &mGuiComm; }
    
    /**
     * @brief Get ICancellable interface to interrupt main thread.
     * @retval Pointer to ICancellable for thread interruption.
     */
    OS::ICancellable* getAppCancellable() {return &mCommonQueue; }

    // =========================================================================
    // Kernel-side API (common queue management)
    // =========================================================================
    
    /**
     * @brief Receive message from application (Service or GUI)
     * @param msg Reference to message pointer (output)
     * @param timeoutMs Timeout in milliseconds
     * @retval true Message received successfully
     * @retval false Timeout or error
     * 
     * @note Reads from common queue (both Service and GUI send here)
     * @note Use msg->getProcessId() to determine sender
     */
    bool receiveFromApp(SDK::MessageBase*& msg, uint32_t timeoutMs);
    
    /**
     * @brief Send message to common queue for internal purposes
     * @param msg Message to send
     * @param timeoutMs Timeout in milliseconds (default 10ms)
     * @retval true Message sent successfully
     * @retval false Queue full or othe error
     *
     * @note Routes to Service or GUI based on msg->getProcessId()
     * @note Returns false if PID doesn't match Service or GUI
     */
    bool sendToApp(SDK::MessageBase* msg, uint32_t timeoutMs = 0);

    /**
     * @brief Send message to Service
     * @param msg Message to send
     * @param pushTimeoutMs How long to wait for room in a full queue. The
     *        default rides out a queue that is only briefly full. Pass 0 from
     *        any caller holding a lock others need - sensor delivery does -
     *        so a service that has stopped draining cannot stall it.
     *        0 never waits on the queue: when it is full it yields once, so a
     *        receiver of the same priority that is merely behind can drain,
     *        and then tries once more. The yield can let other ready tasks of
     *        that priority run first, each for up to a time slice.
     * @retval true Message sent successfully
     * @retval false Queue full or timeout
     *
     * @note Places message in Service receive queue
     * @note Does not wait for a response.
     */
    bool sendToService(SDK::MessageBase* msg, uint32_t pushTimeoutMs = SEND_TIMEOUT_MS);

    /**
     * @brief Send message to GUI
     * @param msg Message to send
     * @param pushTimeoutMs As for sendToService().
     * @retval true Message sent successfully
     * @retval false Queue full or timeout
     *
     * @note Places message in GUI receive queue
     * @note Does not wait for a response.
     */
    bool sendToGui(SDK::MessageBase* msg, uint32_t pushTimeoutMs = SEND_TIMEOUT_MS);

    /**
     * @brief Clear service queue and return messages to pool
     */
    void clearServiceQueue();

    /**
     * @brief Clear GUI queue and return messages to pool
     */
    void clearGuiQueue();

    /**
     * @brief Clear App queue and return messages to pool
     */
    void clearCommonQueue();

    /**
     * @brief Clear all queues and return messages to pool
     * 
     * @note Drains all three queues (common, Service, GUI)
     * @note Releases all messages back to pool
     * @note Useful during shutdown or reset
     */
    void clearAllQueues();

    // =========================================================================
    // Process identification
    // =========================================================================
    
    /**
     * @brief Get Service process ID
     * @retval Service PID
     */
    uint32_t getServicePID() const { return mServicePID; }
    
    /**
     * @brief Get GUI process ID
     * @retval GUI PID
     */
    uint32_t getGuiPID() const { return mGuiPID; }
    
private:
    // =========================================================================
    // Internal IAppComm implementation for Service and GUI
    // =========================================================================
    
    class InternalAppComm : public SDK::Interface::IAppComm {
    public:
        InternalAppComm(uint32_t pid,
                       App::MessageManager& msgMgr,
                       OS::Queue<SDK::MessageBase*, COMMON_QUEUE_SIZE>& commonQueue,
                       OS::Queue<SDK::MessageBase*, SERVICE_QUEUE_SIZE>& serviceQueue,
                       OS::Queue<SDK::MessageBase*, GUI_QUEUE_SIZE>& guiQueue,
                       bool isService)
            : mPID(pid)
            , mMessageManager(msgMgr)
            , mCommonQueue(commonQueue)
            , mServiceQueue(serviceQueue)
            , mGuiQueue(guiQueue)
            , mIsService(isService)
        {}
        
        uint32_t getProcessId() const override { return mPID; }
        
        void releaseMessage(SDK::MessageBase* msg) override {
            // Give the claim back before the reference. A claim means "this
            // process still owes a reference"; leaving it after the process
            // has returned one would let a later sweep drop a reference
            // belonging to somebody else.
            mMessageManager.removeMessageOwner(msg, mPID);
            mMessageManager.releaseMessage(msg);
        }

        void* allocateMessage(size_t size) override {
            void* mem = mMessageManager.allocateRawMemory(size);
            // Owed from the moment it is handed out. A process killed holding
            // something it allocated -- before sending it, or after sending it
            // but before its own release -- would otherwise strand the block
            // with nothing recording that it was owed.
            //
            // The block is raw here: allocateRawMemory has zeroed it and the
            // caller has not yet placement-new'd a message into it. A sweep
            // that finds a claimed block with a zero reference count knows
            // that is what it is looking at, and hands it back without running
            // a destructor -- see MessageManager::releaseByOwner.
            mMessageManager.addMessageOwner(
                    static_cast<SDK::MessageBase*>(mem), mPID);
            return mem;
        }
        
        bool sendMessage(SDK::MessageBase* msg, uint32_t timeoutMs = 0) override;
        bool getMessage(SDK::MessageBase*& msg, uint32_t timeoutMs = osWaitForever) override;
        void sendResponse(SDK::MessageBase* msg) override;
        
    private:
        uint32_t mPID;
        MessageManager& mMessageManager;
        OS::Queue<SDK::MessageBase*, COMMON_QUEUE_SIZE>& mCommonQueue;   // To kernel
        OS::Queue<SDK::MessageBase*, SERVICE_QUEUE_SIZE>& mServiceQueue; // From/to Service
        OS::Queue<SDK::MessageBase*, GUI_QUEUE_SIZE>& mGuiQueue;         // From/to GUI
        bool mIsService;  // true if Service, false if GUI

        /// sendMessage() queue-full failures, throttled, one per
        /// destination: messages to the peer process and messages to the
        /// kernel go to different queues, and one full queue must not hide
        /// the other's first failure.
        LogThrottle mPeerFullLog;
        LogThrottle mKernelFullLog;
    };
    
    // =========================================================================
    // Private members
    // =========================================================================
    
    const uint32_t mServicePID;
    const uint32_t mGuiPID;
    App::MessageManager& mMessageManager;
    
    // Three queues
    OS::Queue<SDK::MessageBase*, COMMON_QUEUE_SIZE> mCommonQueue;   // Service/GUI -> Kernel
    OS::Queue<SDK::MessageBase*, SERVICE_QUEUE_SIZE> mServiceQueue;  // Kernel -> Service
    OS::Queue<SDK::MessageBase*, GUI_QUEUE_SIZE> mGuiQueue;          // Kernel -> GUI
    
    // Two IAppComm interfaces
    InternalAppComm mServiceComm;
    InternalAppComm mGuiComm;

    /// Queue-full failures, throttled: sendToApp() onto this app's
    /// kernel-bound common queue, and sendToService()/sendToGui() into its two
    /// processes. One per queue and per app, so one app that has stopped
    /// draining cannot hide the first failure of another, and a full GUI queue
    /// cannot hide a full service queue.
    LogThrottle mCommonFullLog;
    LogThrottle mServiceFullLog;
    LogThrottle mGuiFullLog;
    
    // Prevent copying
    DualAppComm(const DualAppComm&) = delete;
    DualAppComm& operator=(const DualAppComm&) = delete;
};

} // namespace App

#endif // __APP_DUAL_APP_COMM_HPP
