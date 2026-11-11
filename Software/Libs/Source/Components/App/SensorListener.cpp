#define LOG_MODULE_PRX      "Sensor.Listener"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "App/SensorListener.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "OS.hpp"

#if LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG
// Only the debug trace names the driver. Kept out of other builds so this
// file does not bring the sensor layer with it - the host tests build it alone.
#include "SensorLayer/Core/SensorManager.hpp"
#endif

#include <cstring>
#include <cstddef>

namespace App
{

SensorListener::SensorListener(App::DualAppComm& comm, bool isService)
    : mComm(comm)
    , mIsService(isService)
    , mMaxPoolSize(mComm.getMsgManager().getPool().getMaxBlockSize())
{}

void SensorListener::onSdlNewData(uint16_t                 handle,
                                  const SDK::Sensor::Data* data,
                                  uint16_t                 count,
                                  uint16_t                 stride)
{
    const uint8_t* dataPtr  = reinterpret_cast<const uint8_t*>(data);
    const uint32_t baseSize = sizeof(SDK::Message::Sensor::EventData) - sizeof(SDK::Sensor::Data);

    [[maybe_unused]] uint32_t pid = mIsService ? mComm.getServicePID() : mComm.getGuiPID();

#if LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG
    if (Sensor::Driver* d = Sensor::Manager::getInstance().getDriverByHandle(handle)) {
        LOG_DEBUG("Sensor data. PID 0x%08X. Handle %s\n", pid, d->getDescription());
    } else {
        LOG_DEBUG("Sensor data. PID 0x%08X. Handle %u.\n", pid, handle);
    }
#endif

    while (count) {
        const uint32_t size = count * stride;

        uint32_t packageSize = baseSize + size;

        if (packageSize > mMaxPoolSize) {
            packageSize = mMaxPoolSize;
        }

        const uint32_t payload = packageSize - baseSize;

        uint32_t strideCount = payload / stride;
        if (strideCount == 0) {
            LOG_ERROR("strideCount == 0. Handle %u. PID 0x%08X\n", handle, pid);
            return;
        }

        packageSize = baseSize + strideCount * stride;

        uint8_t* pool = static_cast<uint8_t*>(mComm.getMsgManager().allocateRawMemory(packageSize));
        if (!pool) {
            // Throttled. The first line keeps its old text - field
            // tooling matches on it.
            const LogThrottle::Decision d = mAllocFailLog.onFailure(OS::GetTimeMs());
            if (d.action == LogThrottle::Action::FIRST) {
                LOG_ERROR("Failed to allocate memory for sample. Handle %u. PID 0x%08X\n", handle, pid);
            } else if (d.action == LogThrottle::Action::SUMMARY) {
                // No handle: one listener serves every handle the process
                // subscribed to, so the count spans all of them.
                LOG_ERROR("Failed to allocate memory for sample. PID 0x%08X. %u more since the last report\n",
                        pid, static_cast<unsigned>(d.suppressed));
            }
            return;
        }

        if (const uint32_t failures = mAllocFailLog.onSuccess(OS::GetTimeMs)) {
            LOG_WARNING("Sample allocation for PID 0x%08X recovered after %u failures\n",
                    pid, static_cast<unsigned>(failures));
        }

        auto* event = new (pool) SDK::Message::Sensor::EventData();

        event->handle = handle;
        event->count  = strideCount;
        event->stride = stride;
        memcpy((void*) event->data, (void*) dataPtr, strideCount * stride);

        if (!sendMessage(event)) {
            // Throttled. The first line keeps its old text - field
            // tooling matches on it.
            const LogThrottle::Decision d = mSendFailLog.onFailure(OS::GetTimeMs());
            if (d.action == LogThrottle::Action::FIRST) {
                LOG_ERROR("Failed to send data. Handle %u. PID 0x%08X\n", handle, pid);
            } else if (d.action == LogThrottle::Action::SUMMARY) {
                // No handle, as above.
                LOG_ERROR("Failed to send data. PID 0x%08X. %u more since the last report\n",
                        pid, static_cast<unsigned>(d.suppressed));
            }
        } else if (const uint32_t failures = mSendFailLog.onSuccess(OS::GetTimeMs)) {
            LOG_WARNING("Sample delivery to PID 0x%08X recovered after %u failures\n",
                    pid, static_cast<unsigned>(failures));
        }

        mComm.getMsgManager().releaseMessage(event);

        count   -= strideCount;
        dataPtr += strideCount * stride;
    }
}

bool SensorListener::sendMessage(SDK::MessageBase* msg)
{
    // Never wait for room. Delivery always runs under the driver's
    // mutex, and from the sensor sweep under the sensor manager's mutex too; a
    // push that waits on a full queue waits with those held, so one app that
    // has stopped draining stalls every other sensor client - and teardown,
    // which needs the same locks to disconnect listeners. A zero timeout still
    // yields once before giving up (see sendToService), so an app that is
    // merely behind can catch up; one that has stopped just loses the sample,
    // which is dropped either way.
    if (mIsService) {
        return mComm.sendToService(msg, 0);
    } else {
        return mComm.sendToGui(msg, 0);
    }
}

}
