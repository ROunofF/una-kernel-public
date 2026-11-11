/**
 ******************************************************************************
 * @file    ImageHeap.hpp
 * @brief   Device-side front end for the app image region
 ******************************************************************************
 */

#ifndef IMAGE_HEAP_HPP
#define IMAGE_HEAP_HPP

#include <cstddef>

namespace App {

/**
 * @brief   Allocates app images from the top of RAM downward.
 *
 * @details
 * Wraps App::ImageRegion with the two things it deliberately does not do:
 * knowing where the region is, and excluding the kernel allocator while the
 * floor moves.
 *
 * ### Why there is no lock here
 *
 * `_sbrk_r` runs with the scheduler suspended - `__malloc_lock` and its own
 * critical section are both `vTaskSuspendAll`, and they nest. Every function
 * below takes the same suspension, so an image placement and a break
 * extension cannot interleave, and no new lock order exists to get wrong. The
 * floor is therefore an ordinary pointer rather than an atomic: there is no
 * lock-free protocol, so there is none to get subtly wrong either.
 *
 * That exclusion brings the usual constraints, and they are why the logging
 * in these functions happens after the suspension ends:
 *
 * - **No `LOG_*` while suspended.** The logger takes its mutex with a
 *   non-zero timeout and FreeRTOS's `configASSERT` fires on any attempt to
 *   block while the scheduler is suspended.
 * - **No allocation while suspended**, including anything reaching `malloc`
 *   indirectly - which is the whole reason ImageRegion holds no dynamic
 *   state.
 *
 * Not an ISR concern: app load and unload are task context, and
 * `__malloc_lock` already asserts it is not inside an interrupt.
 */
namespace ImageHeap {

/**
 * @brief   Reserve memory for an app image at the highest address that fits.
 * @param   bytes  Size of the image.
 * @return  Pointer to the reservation, or nullptr if it will not fit.
 *
 * @note    nullptr is an ordinary outcome, not a fault: it means the heap is
 *          genuinely full, which under this scheme is the only way an image
 *          can be refused. The caller reports it and declines to launch.
 */
void* allocate(size_t bytes);

/**
 * @brief   Release an image reservation.
 * @param   base  A pointer previously returned by allocate(). nullptr is
 *                ignored.
 * @return  false if @p base was never handed out - a double release or a
 *          stray pointer. Logged, because there is no later symptom.
 */
bool release(void* base);

/// Largest image that could be placed right now, for reporting a refusal
/// with a number rather than just a failure.
size_t largestFree();

/// Bytes currently reserved for images, excluding gaps between them.
size_t bytesInUse();

/// Images currently resident.
size_t count();

} // namespace ImageHeap

} // namespace App

#endif // IMAGE_HEAP_HPP
