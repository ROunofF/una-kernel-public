#pragma once

#include "App/DualAppComm.hpp"
#include "SDK/Messages/MessageBase.hpp"

namespace App {

template<typename T>
class MessageGuard
{
public:
    MessageGuard(App::DualAppComm& comm, T* ptr, bool toService) noexcept
        : mComm(comm)
        , mPtr(ptr)
        , mToService(toService)
    {}

    MessageGuard(const MessageGuard&)            = delete;
    MessageGuard& operator=(const MessageGuard&) = delete;
    MessageGuard(MessageGuard&&)                 = delete;
    MessageGuard& operator=(MessageGuard&&)      = delete;

    ~MessageGuard() noexcept { cleanup(); }

    T*       get()           const noexcept { return mPtr;  }
    T*       operator->()    const noexcept { return mPtr;  }
    T&       operator*()     const noexcept { return *mPtr; }
    explicit operator bool() const noexcept { return mPtr != nullptr; }

    bool send()
    {
        if (!mPtr) {
            return false;
        }

        if (mToService) {
            return mComm.sendToService(mPtr);
        } else {
            return mComm.sendToGui(mPtr);
        }
    }

    bool ok() const noexcept
    {
        return mPtr/* && (mPtr->getResult() == SDK::MessageResult::SUCCESS)*/; // Result PENDING
    }

private:
    void cleanup() noexcept
    {
        if (mPtr) {
            mComm.getMsgManager().releaseMessage(mPtr);
            mPtr = nullptr;
        }
    }

    App::DualAppComm& mComm;
    T*                mPtr;
    bool              mToService;
};

template<typename T>
MessageGuard<T> make_msg_to_srv(App::DualAppComm& comm)
{
    T* raw = comm.getMsgManager().template allocateMessage<T>();
    return MessageGuard<T>(comm, raw, true);
}

template<typename T>
MessageGuard<T> make_msg_to_gui(App::DualAppComm& comm)
{
    T* raw = comm.getMsgManager().template allocateMessage<T>();
    return MessageGuard<T>(comm, raw, false);
}

template<typename T>
MessageGuard<T> make_msg(App::DualAppComm& comm, bool toService)
{
    T* raw = comm.getMsgManager().template allocateMessage<T>();
    return MessageGuard<T>(comm, raw, toService);
}

} // namespace App
