/**
 ******************************************************************************
 * @file    IconPool.hpp
 * @brief   Fixed storage for the app icons the screens are showing.
 ******************************************************************************
 */

#ifndef COMPONENT_APP_ICON_POOL_HPP
#define COMPONENT_APP_ICON_POOL_HPP

// Relative to this header rather than by bare name: the GUI simulator builds
// against this file without Components/Shared on its include path.
#include "../Shared/AppTypes.hpp"

#include <atomic>
#include <cstdint>

namespace App
{

/// Most icon sets one window can name. Bounds the events that carry them and
/// the storage they are carried from.
constexpr uint8_t kIconWindowMax = 16;

/// One pooled set and the count of references to it. Not used directly --
/// IconRef is the handle.
struct IconSlot {
    IconSet set {};

    /// Zero means free. Read and written from both the GUI and the backend
    /// task, so it must be atomic: see the note on IconRef.
    std::atomic<uint16_t> refs { 0 };
};

/**
 * @class IconRef
 * @brief A counted reference to a pooled IconSet, safe to pass between tasks.
 *
 * Deliberately not std::shared_ptr. libstdc++ here is built without gthreads:
 * __gnu_cxx::__default_lock_policy is _S_single, and asking for _S_atomic
 * explicitly changes nothing because __exchange_and_add_dispatch resolves
 * __is_single_threaded() at compile time. A shared_ptr copy on this target is
 * therefore a plain load-add-store on the control block.
 *
 * That is not good enough here. These sets are built on the backend task and
 * released on the GUI task -- both osPriorityNormal, so FreeRTOS slices between
 * them at arbitrary instructions -- and a lost increment frees a set that is
 * still being drawn from. std::atomic<uint16_t> compiles to ldrexh/strexh on
 * this core, so the count below is genuinely atomic.
 *
 * Copying is what takes a reference; letting go of the last one frees the slot
 * for IconPool::take(). Nothing has to be called by hand.
 */
class IconRef
{
public:
    IconRef() = default;

    ~IconRef()
    {
        release();
    }

    IconRef(const IconRef& other) : mSlot(other.mSlot)
    {
        retain();
    }

    IconRef(IconRef&& other) noexcept : mSlot(other.mSlot)
    {
        other.mSlot = nullptr;
    }

    IconRef& operator=(const IconRef& other)
    {
        if (this != &other) {
            // Taken before the old one is let go: assigning a reference to the
            // slot this already holds must not let its count touch zero on the
            // way through, or the pool could hand the slot out mid-assignment.
            IconSlot* incoming = other.mSlot;
            if (incoming != nullptr) {
                incoming->refs.fetch_add(1, std::memory_order_relaxed);
            }

            release();
            mSlot = incoming;
        }

        return *this;
    }

    IconRef& operator=(IconRef&& other) noexcept
    {
        if (this != &other) {
            release();
            mSlot = other.mSlot;
            other.mSlot = nullptr;
        }

        return *this;
    }

    /// Drop the reference; the handle is empty afterwards.
    void reset()
    {
        release();
        mSlot = nullptr;
    }

    explicit operator bool() const { return mSlot != nullptr; }

    const IconSet* get() const { return (mSlot != nullptr) ? &mSlot->set : nullptr; }
    const IconSet* operator->() const { return &mSlot->set; }

    /// The set, to be filled in by whoever just took it from the pool. Only
    /// meaningful before the handle has been shared with anyone else.
    IconSet& mutableSet() { return mSlot->set; }

private:
    friend class IconPool;

    explicit IconRef(IconSlot* slot) : mSlot(slot)
    {
        retain();
    }

    void retain()
    {
        if (mSlot != nullptr) {
            mSlot->refs.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void release()
    {
        if (mSlot != nullptr) {
            // Release ordering so that everything written through this handle
            // is visible to whoever sees the count fall to zero and reuses the
            // slot.
            mSlot->refs.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    IconSlot* mSlot = nullptr;
};

/**
 * @class IconPool
 * @brief Where an @ref IconSet lives while a menu is drawing it.
 *
 * A pair of icons is about 4.5 KB, and a menu asks for a fresh window of them
 * every time its selection moves -- so on the heap this was kilobyte-scale
 * churn on the input path, at up to ten a second while a button is held, on a
 * device that runs for weeks. Worse, with @c -fno-exceptions a failed @c new
 * calls @c abort() (see the note in heap_useNewlib_ST.cpp), so an icon that
 * could not be allocated would take the watch down rather than simply not be
 * drawn.
 *
 * Here the sets are static and the slots are recycled, and nothing on this path
 * touches the heap at all: the pixels are read straight into the slot and the
 * handles carry no allocation of their own.
 *
 * Running out is not a failure: take() returns an empty handle, that app is
 * left out of the reply, and its item draws its label with no icon -- which is
 * what it does anyway in the moments before its icons arrive.
 *
 * Header-only deliberately: a new .cpp in this tree is silently left out of the
 * build until it is also listed in the CubeIDE .project.
 */
class IconPool
{
public:
    /**
     * @brief Slots, and so the most sets that can be in use at once.
     *
     * Fifteen are held for as long as the watch is on -- the arc's window of
     * eight and the wheel's of seven, kept so either menu opens complete on its
     * first frame. On top of that, refreshing one of those windows holds the
     * sets going out and the ones arriving at the same time, and a reply still
     * queued pins whatever it carries.
     *
     * Measured, not reasoned: a peak of 23 with thirty apps installed, the
     * wheel scrolling continuously and replies arriving a frame late. Sized
     * above that with room, because running out is silent -- the icon is
     * simply not drawn.
     *
     * Independent of how long the lists get: what a menu asks for is its
     * window, not its list, so installing more apps does not raise this.
     */
    static constexpr uint8_t kSlots = 32;

    /**
     * @brief A cleared set to fill, or an empty handle if every slot is in use.
     *
     * @note Call from one thread only -- on the watch, the backend task. A free
     *       slot is one whose count has reached zero, and nothing can raise it
     *       from zero except this function, because a count of zero means no
     *       handle to that slot exists to be copied. Giving a slot back is
     *       thread-safe and needs no call: it is the last handle going away,
     *       wherever that happens.
     */
    static IconRef take()
    {
        for (uint8_t i = 0; i < kSlots; ++i) {
            if (sSlots[i].refs.load(std::memory_order_acquire) != 0) {
                continue;   // a widget is still drawing from it
            }

            // Cleared on the way out rather than on the way back: a caller
            // fills only what it could read, and the previous tenant's pixels
            // would show through the rest.
            sSlots[i].set = IconSet{};

            return IconRef(&sSlots[i]);
        }

        return IconRef();
    }

    /// Sets currently in use. For a log line or a test; nothing decides on it.
    static uint8_t inUse()
    {
        uint8_t used = 0;

        for (uint8_t i = 0; i < kSlots; ++i) {
            if (sSlots[i].refs.load(std::memory_order_relaxed) != 0) {
                ++used;
            }
        }

        return used;
    }

private:
    static inline IconSlot sSlots[kSlots] {};
};

} // namespace App

#endif // COMPONENT_APP_ICON_POOL_HPP
