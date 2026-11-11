/**
 ******************************************************************************
 * @file    Kernel.hpp
 * @brief   Concrete Kernel implementation.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __APP_KERNEL_HPP
#define __APP_KERNEL_HPP

#include <cstdint>
#include <cstddef>

#include "SDK/Interfaces/IKernel.hpp"
#include "SDK/Messages/MessageBase.hpp"

#include "App/Logger.hpp"
#include "App/MemAllocator.hpp"

namespace App
{

class MessageManager;

class Kernel : public SDK::Interface::IKIP,
               public SDK::Interface::ISystem
{
public:
    Kernel(const char *name,
            SDK::Interface::IAppComm &comm,
            App::MessageManager &msgMgr,
            SDK::Interface::IFileSystem &fs);

    virtual ~Kernel();

    SDK::Interface::IKernel* getInterface() { return &mIKernel; }

    void reset();

    // NOTE!
    // Use this only after getting SDK::Message::RequestAppTerminate
    void waitReadyToDelete();

private:
    /// Application name
    const char* mName;

    /// Logger
    App::Logger mLogger;

    /// Memory allocator
    App::MemAllocator mMemAllocator;

    /// Communication interface
    SDK::Interface::IAppComm &mComm;

    /// Message manager, used ONLY to allocate the terminate reserve. That has
    /// to bypass mComm: allocating through the process's own IAppComm records
    /// the process as owing the block, and this reserve is held by the kernel
    /// for the whole life of the AppInstance -- so a sweep of a process that
    /// died without calling exit() would free it while this object still
    /// points at it.
    App::MessageManager &mMessageManager;

    /// File system
    SDK::Interface::IFileSystem &mFs;

    /// IKernel instance
    SDK::Interface::IKernel mIKernel;


    /// Flag for synchronization with external control thread
    bool isReadyToDelete;

    /// Terminate message reserved at construction so exit() never has to
    /// allocate from the shared pool (an app frequently exits precisely because
    /// it is out of resources). Kernel-owned across the app's lifetime and
    /// reused on every exit(); released in the destructor.
    SDK::MessageBase* mTerminateMsg;

    // IKIP
    virtual void* queryInterface(IntfID iid) override;

    // ISystem
    virtual void exit(int status = 0) override;
    virtual uint32_t getTimeMs() override;
    virtual void delay(uint32_t ms) override;
    virtual void yield() override;

};

} // namespace App

#endif // __APP_KERNEL_HPP
