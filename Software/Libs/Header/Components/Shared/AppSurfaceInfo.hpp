/**
 ******************************************************************************
 * @file    AppSurfaceInfo.hpp
 * @brief   Where a running app's frame can be read from.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SHARED_APP_SURFACE_INFO_HPP
#define SHARED_APP_SURFACE_INFO_HPP

#include <atomic>
#include <cstdint>

/**
 * @brief Where a running app's frame can be read from.
 *
 * The frame an app produced, copied aside so the GUI can compose it under its
 * own chrome. Announced once during startup rather than carried per frame: the
 * address is fixed for the lifetime of the system and only the content behind
 * it changes. pixels is null where no app can run, which is how the simulator
 * sees it.
 *
 * Everything that changes while the system runs is reached through a pointer
 * for that reason -- a copy of this struct is cached by the reader and would
 * otherwise freeze at the values it was announced with.
 *
 * owner is the app the pixels belong to, and generation counts frames accepted
 * since the surface last changed hands. Together they answer the only question
 * a reader has: is there a frame here, and is it mine. Zero generation means
 * the current owner has not produced anything yet.
 */
struct AppSurfaceInfo {
    const uint8_t *pixels = nullptr;    ///< frame, in panel format
    int16_t        width  = 0;
    int16_t        height = 0;

    const std::atomic<uint32_t> *generation = nullptr;
    const std::atomic<uint32_t> *owner      = nullptr;

    /**
     * @brief Read the owner and the frame counter as a pair.
     *
     * They are two atomics, so they cannot be read in one go, and a handover
     * landing between the two reads would pair one app's identity with
     * another's counter. The owner is therefore read on both sides: if it
     * moved, the caller is told to leave the surface alone rather than act on
     * a pair that never existed. The next tick sees a settled one.
     *
     * @param ownerPid  App the pixels belong to, 0 if none.
     * @param frame     Frames that app has produced.
     * @return false when there is no surface, or the owner changed mid-read.
     */
    bool sample(uint32_t &ownerPid, uint32_t &frame) const
    {
        ownerPid = 0;
        frame    = 0;

        if (owner == nullptr || generation == nullptr) {
            return false;
        }

        const uint32_t before = owner->load(std::memory_order_acquire);
        const uint32_t seen   = generation->load(std::memory_order_acquire);
        const uint32_t after  = owner->load(std::memory_order_acquire);

        if (before != after) {
            return false;
        }

        ownerPid = before;
        frame    = seen;

        return true;
    }
};

#endif // SHARED_APP_SURFACE_INFO_HPP
