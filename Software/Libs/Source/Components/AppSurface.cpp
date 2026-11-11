/**
 ******************************************************************************
 * @file    AppSurface.cpp
 * @brief   Frame surface a user app draws into, for the kernel GUI to compose.
 ******************************************************************************
 *
 ******************************************************************************
 */

// Handovers are logged at debug. Raise this to LOG_LEVEL_DEBUG to watch who
// holds the surface; at info the calls compile away and the lines stay.
#define LOG_MODULE_PRX      "AppSurface"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "AppSurface.hpp"
#include "ILcd.hpp"
#include "WordMove.hpp"


AppSurface::AppSurface()
{
    mMutex.init(LOG_MODULE_PRX);
}

bool AppSurface::init(Interface::ILcd &lcd)
{
    int16_t width  = 0;
    int16_t height = 0;
    lcd.getResolution(width, height);

    // A panel the buffer cannot hold would be accepted frame by frame and
    // truncated silently, so refuse the binding instead.
    if (width != skWidth || height != skHeight) {
        LOG_ERROR("Panel %dx%d does not fit the surface %dx%d\n",
                  width, height, skWidth, skHeight);
        return false;
    }

    // Unlocked on purpose: this runs at panel bring-up, before the backend and
    // TouchGFX are started and therefore before anything can read it, and it
    // is the only write. A lock here would suggest a race that cannot happen.
    mpLcd = &lcd;

    return true;
}

void AppSurface::setOwner(uint32_t pid)
{
    if (!mMutex.lock()) {
        return;
    }

    // Compared under the lock rather than before it, so the owner is never
    // read while another caller is part way through changing it.
    const bool changed = (mOwnerPid.load(std::memory_order_relaxed) != pid);
    if (changed) {
        // The buffer holds the current owner's pixels or nothing. Keeping that
        // invariant here is cheap -- once per handover -- and it means a reader
        // deciding on the frame counter alone is still correct.
        Tools::ZeroWordwise(mBuffer, sizeof(mBuffer));

        // Back to zero rather than bumped: zero is how a reader tells that the
        // new owner has not put anything there yet. Stored before the owner, and
        // the owner with release, so whoever sees the new owner sees this too.
        mGeneration.store(0, std::memory_order_relaxed);
        mOwnerPid.store(pid, std::memory_order_release);
    }

    mMutex.release();

    if (!changed) {
        return;
    }

    LOG_DEBUG("Owner 0x%08X\n", static_cast<unsigned>(pid));
}

void AppSurface::releaseOwner(uint32_t pid)
{
    if (pid == 0 || !mMutex.lock()) {
        return;
    }

    // The owner only. The pixels are deliberately left alone: a screen still
    // showing this app reads the change of owner and holds its last frame
    // rather than being handed a blank one to draw over it. Whoever takes the
    // surface next is what clears it.
    const bool changed = (mOwnerPid.load(std::memory_order_relaxed) == pid);
    if (changed) {
        mOwnerPid.store(0, std::memory_order_release);
    }

    mMutex.release();

    if (!changed) {
        return;
    }

    LOG_DEBUG("Released by 0x%08X\n", static_cast<unsigned>(pid));
}

bool AppSurface::lock()
{
    return mMutex.lock();
}

void AppSurface::unlock()
{
    mMutex.release();
}

void AppSurface::getResolution(int16_t &width, int16_t &height) const
{
    if (mpLcd == nullptr) {
        width  = 0;
        height = 0;
        return;
    }

    mpLcd->getResolution(width, height);
}

uint8_t AppSurface::getDisplayColorDepth() const
{
    if (mpLcd == nullptr) {
        return 0;
    }

    return mpLcd->getDisplayColorDepth();
}

bool AppSurface::write(uint32_t pid, const uint8_t *pBuf)
{
    // No panel bound means init() rejected the geometry, and the copy below
    // reads a whole frame's worth from a buffer that is then not that size --
    // so it stops here rather than reading past the end of it. A zero pid is
    // no app, and must not match an unowned surface.
    if (pid == 0 || pBuf == nullptr || mpLcd == nullptr) {
        return false;
    }

    if (!mMutex.lock()) {
        return false;
    }

    // Under the same lock as the copy on purpose. Ownership is changed from
    // the lifecycle thread, so a check made anywhere outside this lock could
    // already be stale by the time the copy runs.
    const bool owned = (mOwnerPid.load(std::memory_order_relaxed) == pid);
    if (owned) {
        static_assert(sizeof(mBuffer) % sizeof(uint32_t) == 0,
                      "frame must be a whole number of words");
        Tools::CopyWordwise(mBuffer, pBuf, sizeof(mBuffer));

        // After the copy and still under the lock, so it cannot land after a
        // handover reset it and claim the incoming app has drawn.
        mGeneration.fetch_add(1, std::memory_order_relaxed);
    }

    mMutex.release();

    // After the lock, so what is woken by this can read the frame straight
    // away instead of queueing behind the copy that produced it.
    if (owned && mpCallback != nullptr) {
        mpCallback->onFrame();
    }

    return owned;
}
