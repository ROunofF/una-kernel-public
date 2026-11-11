/**
 ******************************************************************************
 * @file    ImageHeap.cpp
 * @brief   Device-side front end for the app image region
 ******************************************************************************
 */

#include "App/ImageHeap.hpp"
#include "App/ImageRegion.hpp"

#include "FreeRTOS.h"
#include "task.h"

#define LOG_MODULE_PRX      "ImageHeap"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

extern "C" {
/// End of the kernel arena. Defined in heap_useNewlib_ST.cpp; read inside the
/// same suspension as the placement it informs, or it is stale on return.
char *Heap_currentBreak(void);

/// Publish the base of the lowest resident image, or NULL for none. Must be
/// called with the scheduler suspended.
void Heap_setImageFloor(char *floor);

/// Top of the image region: __HeapLimit less the ISR stack, so the ISR stack
/// is never inside it. Defined alongside the linker symbols it reads.
char *Heap_imageRegionTop(void);
}

namespace {

/// File scope rather than a function-local static on purpose: every member
/// is zero-initialised, so this needs no dynamic initialisation and no guard
/// variable. A function-local static would introduce __cxa_guard_acquire on
/// a path that runs from arbitrary tasks, and the guard is not atomic in this
/// build.
App::ImageRegion gRegion;
bool             gInitialised = false;

/// Caller must hold the suspension.
void ensureInitLocked()
{
    if (!gInitialised) {
        gRegion.init(reinterpret_cast<uintptr_t>(Heap_imageRegionTop()));
        gInitialised = true;
    }
}

} // namespace

namespace App {
namespace ImageHeap {

void* allocate(size_t bytes)
{
    uintptr_t base      = 0;
    size_t    largest   = 0;
    size_t    resident  = 0;

    vTaskSuspendAll();
    {
        ensureInitLocked();

        const uintptr_t brk = reinterpret_cast<uintptr_t>(Heap_currentBreak());

        base = gRegion.allocate(bytes, brk);

        if (base != 0) {
            // Lowering the floor must be published before the caller writes
            // into the region, and it is: the break cannot move while the
            // scheduler is suspended, so no allocation can have crossed it
            // between the decision and the publication.
            Heap_setImageFloor(reinterpret_cast<char*>(gRegion.floor()));
        } else {
            largest  = gRegion.largestFreeBlock(brk);
            resident = gRegion.count();
        }
    }
    xTaskResumeAll();

    // Logged out here: the logger blocks on its mutex, which FreeRTOS forbids
    // while the scheduler is suspended.
    if (base == 0) {
        // Say WHICH limit refused it. A full region and a full extent table
        // look identical from the caller and want opposite responses - one is
        // the heap being genuinely out of room, the other is skMaxImages being
        // too small, which the peak of 9 across the bench captures says it is
        // not. Without this they would be indistinguishable in a field log.
        if (resident >= ImageRegion::skMaxImages) {
            LOG_ERROR("no slot for a %u byte image; all %u are in use\n",
                      static_cast<unsigned>(bytes),
                      static_cast<unsigned>(resident));
        } else {
            LOG_ERROR("no room for a %u byte image; largest free is %u "
                      "across %u resident\n",
                      static_cast<unsigned>(bytes),
                      static_cast<unsigned>(largest),
                      static_cast<unsigned>(resident));
        }
    }

    return reinterpret_cast<void*>(base);
}

bool release(void* base)
{
    if (base == nullptr) {
        return true;
    }

    bool ok = false;

    vTaskSuspendAll();
    {
        ok = gRegion.release(reinterpret_cast<uintptr_t>(base));

        // Raising the floor may lag safely - a stale low floor only makes the
        // kernel more conservative - but there is no reason to let it, and
        // NULL when nothing is resident says "no region active" plainly
        // rather than relying on the top happening to equal the old limit.
        Heap_setImageFloor(gRegion.count() == 0
                ? nullptr
                : reinterpret_cast<char*>(gRegion.floor()));
    }
    xTaskResumeAll();

    if (!ok) {
        LOG_ERROR("release of 0x%08X which was never handed out\n",
                  static_cast<unsigned>(reinterpret_cast<uintptr_t>(base)));
    }

    return ok;
}

size_t largestFree()
{
    size_t largest = 0;

    vTaskSuspendAll();
    {
        ensureInitLocked();
        largest = gRegion.largestFreeBlock(
                reinterpret_cast<uintptr_t>(Heap_currentBreak()));
    }
    xTaskResumeAll();

    return largest;
}

size_t bytesInUse()
{
    vTaskSuspendAll();
    const size_t n = gRegion.bytesInUse();
    xTaskResumeAll();
    return n;
}

size_t count()
{
    vTaskSuspendAll();
    const size_t n = gRegion.count();
    xTaskResumeAll();
    return n;
}

} // namespace ImageHeap
} // namespace App
