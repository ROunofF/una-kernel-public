/**
 ******************************************************************************
 * @file    AppSurface.hpp
 * @brief   Frame surface a user app draws into, for the kernel GUI to compose.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_APP_SURFACE_HPP
#define COMPONENT_APP_SURFACE_HPP

#include <atomic>
#include <cstdint>
#include <cstdbool>

#include "AppSurfaceInfo.hpp"
#include "OS.hpp"

// Only referred to here, by reference and by pointer
namespace Interface { class ILcd; }

/**
 * @brief   Frame surface a user app draws into.
 *
 *          Stands where the panel used to for a running app: the app hands
 *          over whole frames exactly as it would to a display, and never
 *          learns that they are being composed rather than clocked out.
 *          Resolution and colour depth are answered from the real panel, so
 *          the app configures itself against the hardware it will actually
 *          appear on.
 *
 *          Deliberately not an Interface::ILcd. A display accepts whatever it
 *          is given; this only accepts frames from the app that holds the
 *          surface, and that check is the whole point of the type.
 *
 *          A frame is copied into a buffer the surface owns, which detaches it
 *          from the app's own framebuffer -- the app is free to redraw the
 *          moment the copy returns, and the kernel reads a snapshot that
 *          cannot change under it.
 *
 *          Two threads meet here: whichever one delivers app frames, and the
 *          GUI thread that reads the buffer while it renders. lock() and
 *          unlock() bracket the reader; write() takes the same lock for the
 *          duration of the copy.
 */
class AppSurface {

public:

    /// Frame geometry. Checked against the panel in init().
    static const int16_t skWidth  = 240;
    static const int16_t skHeight = 240;

    /**
     * @brief   Get AppSurface instance.
     * @retval  AppSurface instance.
     */
    static AppSurface& GetInstance()
    {
        static AppSurface sInstance;
        return sInstance;
    }

    /**
     * @brief   Bind the real panel, which answers for resolution and depth.
     * @param   lcd: panel instance.
     * @retval  Execution status. 'true' - the panel matches the buffer,
     *          'false' - geometry mismatch, the surface stays unusable.
     */
    bool init(Interface::ILcd &lcd);

    /**
     * @brief   Say which app the surface now belongs to.
     *
     *          Content only means anything in the context of the app that drew
     *          it, so a change of owner drops it. Without this the next app
     *          shows the previous one's last frame until it draws its own --
     *          and switching apps suspends rather than stops the old one, so
     *          there is no exit to hang the reset on.
     *
     *          Same owner is a no-op: an app coming back from suspend still
     *          owns what is in the buffer, and blanking it would cost a frame
     *          for nothing.
     *
     * @param   pid: GUI process the surface now belongs to.
     */
    void setOwner(uint32_t pid);

    /**
     * @brief   Give the surface up, if this app is the one holding it.
     *
     *          Apps are stopped in the background too, so a stopping one must
     *          not disturb whichever app is actually on screen -- hence the
     *          check rather than an unconditional release.
     *
     *          Only the owner is dropped; the pixels stay. A screen still
     *          showing the app then knows the surface is no longer its own and
     *          holds the last frame until it leaves, which is better than
     *          blanking for the tick or two that takes.
     *
     * @param   pid: GUI process giving the surface up.
     */
    void releaseOwner(uint32_t pid);

    /**
     * @brief   Take the surface for reading.
     *
     *          Held across the GUI's draw pass and nothing else -- never
     *          across a panel write, which would stall the app for the whole
     *          frame time for no reason.
     *
     * @retval  Execution status. 'true' - taken, 'false' - otherwise.
     */
    bool lock();

    /**
     * @brief   Release the surface after reading.
     */
    void unlock();

    /**
     * @brief   Describe the surface to a reader.
     *
     *          Everything in here is fixed for the lifetime of the system, so
     *          a reader is told once rather than per frame. The pixels are
     *          only meaningful between lock() and unlock(); the frame counter
     *          can be sampled at any time.
     *
     *          An empty description when no panel is bound, because init()
     *          then refused the geometry and write() drops every frame. A
     *          reader takes its "nothing will ever be drawn" path on that and
     *          says so, rather than waiting out a first frame that cannot
     *          arrive.
     *
     * @retval  Surface description, empty if the surface is unusable.
     */
    AppSurfaceInfo getInfo() const
    {
        if (mpLcd == nullptr) {
            return AppSurfaceInfo { };
        }

        return AppSurfaceInfo { mBuffer, skWidth, skHeight, &mGeneration, &mOwnerPid };
    }

    /**
     * @brief   Panel resolution, for an app asking what it draws on.
     * @param   width: Panel width.
     * @param   height: Panel height.
     */
    void getResolution(int16_t &width, int16_t &height) const;

    /**
     * @brief   Panel colour depth, for an app asking what it draws on.
     * @retval  Bits per pixel.
     */
    uint8_t getDisplayColorDepth() const;

    /**
     * @brief   Accept a frame from an app.
     *
     *          The owner check happens under the same lock as the copy, so it
     *          cannot be separated from it. Ownership changes on the lifecycle
     *          thread, and a caller that checked a moment earlier would drop
     *          a stale frame into a surface already handed to someone else.
     *
     *          Refusing is a normal outcome, not an error: an app suspended a
     *          moment ago may still be mid-frame, and one about to be resumed
     *          may draw before the handover has run.
     *
     * @param   pid: GUI process offering the frame.
     * @param   pBuf: Frame buffer, one whole frame in panel format.
     * @retval  Execution status. 'true' - taken, 'false' - not the owner, or
     *          no panel is bound.
     */
    bool write(uint32_t pid, const uint8_t *pBuf);

    /**
     * @brief   Told when a frame has been taken.
     * @note    Whoever draws the surface has no other way of knowing: nothing
     *          about the buffer changing is visible until it is looked at.
     */
    class Callback {
    public:
        virtual void onFrame() = 0;
        virtual ~Callback() = default;
    };

    /**
     * @brief   Attach the one listener told about incoming frames.
     * @param   pCallback: listener, nullptr to detach.
     */
    void attachCallback(Callback *pCallback) { mpCallback = pCallback; }

private:

    /**
     * @brief   Constructor.
     */
    AppSurface();

    /**
     * @brief   Destructor.
     */
    ~AppSurface() = default;

    // Aligned because a reader may wrap it as a bitmap, which the graphics
    // framework requires to start on a 4-byte boundary.
    alignas(4) uint8_t mBuffer[skWidth * skHeight] { };

    // Whose frame the buffer holds. Atomic because a reader outside the render
    // pass samples it every tick to decide whether the pixels are the ones it
    // means to show, while writers change it under the mutex.
    std::atomic<uint32_t> mOwnerPid { 0 };

    std::atomic<uint32_t> mGeneration { 0 };

    // Told after each frame is taken. Set once at bring-up, before any app can
    // draw, so it is not made atomic.
    Callback *mpCallback { nullptr };

    // The panel, for the properties an app is entitled to ask about. Written
    // once at bring-up, before any thread that reads it exists, so it is not
    // covered by the mutex below.
    Interface::ILcd *mpLcd = nullptr;

    // Serializes the copy in write() against a reader's lock()/unlock()
    OS::Mutex mMutex;

};

#endif // COMPONENT_APP_SURFACE_HPP
