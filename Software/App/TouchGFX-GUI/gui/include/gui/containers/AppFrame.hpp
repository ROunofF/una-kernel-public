#ifndef APPFRAME_HPP
#define APPFRAME_HPP

#include <gui_generated/containers/AppFrameBase.hpp>
#include <touchgfx/widgets/Image.hpp>
#include <touchgfx/Bitmap.hpp>

/**
 * @brief A running app's frame.
 *
 *        Shows pixels the app produced and the kernel copied aside, so a host
 *        screen can put its own chrome on top of them. The pixels are not
 *        owned here and not copied again: the address is fixed for the
 *        lifetime of the system, so attach() is called once per screen and the
 *        content simply changes underneath.
 *
 *        Nothing is shown until the host screen says a frame is worth showing.
 *        The widget cannot tell whose pixels are in the buffer -- only the
 *        screen knows which app it means to display -- so it holds that
 *        decision rather than guessing from the buffer.
 *
 *        Every entry point is safe before attach() and after detach(), and a
 *        host screen relies on it: UserAppView learns which app it is showing
 *        before it is told where the surface is, so showFrame() and hideFrame()
 *        are both reached while there is no bitmap. They report that nothing
 *        was painted, and the attach that follows settles it.
 *
 *        Reading is serialized elsewhere, around the whole render pass, so
 *        nothing here has to lock.
 */
class AppFrame : public AppFrameBase
{
public:
    AppFrame();
    virtual ~AppFrame();

    virtual void initialize();

    /**
     * @brief Point the frame at a buffer and size it to match.
     *
     *        Binding only. Nothing appears until showFrame().
     *
     * @param pixels Externally owned pixels, ABGR2222, 4-byte aligned.
     * @param width  Buffer width.
     * @param height Buffer height.
     * @return true if the buffer can be drawn from.
     */
    bool attach(const uint8_t *pixels, int16_t width, int16_t height);

    /**
     * @brief Drop the bitmap and hide.
     *
     *        Descriptor slots are a small shared pool, so a screen that is no
     *        longer showing an app gives its slot back.
     */
    void detach();

    /**
     * @brief Show the buffer, repainting if it has moved on.
     *
     *        Cheap enough to call from every tick, which is the point -- the
     *        alternative is repainting the whole screen at the GUI's rate no
     *        matter what the app is doing.
     *
     * @param owner Producer the counter below belongs to.
     * @param frame That producer's current frame counter.
     * @return true if something was repainted.
     */
    bool showFrame(uint32_t owner, uint32_t frame);

    /**
     * @brief Stop showing the buffer, leaving whatever is behind it.
     *
     *        For a screen that has moved on to another app: the buffer still
     *        holds the previous one's pixels until that app draws.
     *
     * @return true if something was repainted.
     */
    bool hideFrame();

    /**
     * @brief Whether there is anywhere for an app's pixels to be drawn.
     *
     *        False when attach() has not run or could not get a descriptor, in
     *        which case no frame will ever appear here however many arrive. A
     *        screen that takes its own content down to make room for an app has
     *        to ask, or it takes it down for nothing.
     */
    bool isAttached() const { return mId != touchgfx::BITMAP_INVALID; }

protected:
    touchgfx::Image    mImage;
    touchgfx::BitmapId mId { touchgfx::BITMAP_INVALID };
    uint32_t           mDrawnOwner { 0 };
    uint32_t           mDrawnFrame { 0 };
};

#endif // APPFRAME_HPP
