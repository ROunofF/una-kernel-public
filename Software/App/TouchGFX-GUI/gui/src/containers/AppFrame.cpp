#include <gui/containers/AppFrame.hpp>

AppFrame::AppFrame()
{

}

AppFrame::~AppFrame()
{
    detach();
}

void AppFrame::initialize()
{
    AppFrameBase::initialize();

    add(mImage);
}

bool AppFrame::attach(const uint8_t *pixels, int16_t width, int16_t height)
{
    detach();

    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    mId = touchgfx::Bitmap::dynamicBitmapCreateExternal(
            static_cast<uint16_t>(width), static_cast<uint16_t>(height),
            pixels, touchgfx::Bitmap::ABGR2222);
    if (mId == touchgfx::BITMAP_INVALID) {
        // The descriptor pool is shared and exhausting it is otherwise silent:
        // the frame would just never draw.
        return false;
    }

    // An 8bpp bitmap carries an alpha channel, and a dynamic one starts with no
    // solid rect, which would blit it through the per-pixel path and let bytes
    // the app never painted show whatever is underneath. Declaring the whole
    // frame solid is what makes it opaque, and a straight copy.
    touchgfx::Bitmap::dynamicBitmapSetSolidRect(mId, touchgfx::Rect(0, 0, width, height));

    mImage.setBitmap(touchgfx::Bitmap(mId));
    mImage.setPosition(0, 0, width, height);
    mImage.setVisible(false);
    setWidthHeight(mImage);

    // Whatever is in the buffer now is not this screen's to show, so it counts
    // as nothing drawn.
    mDrawnOwner = 0;
    mDrawnFrame = 0;

    setVisible(true);

    return true;
}

void AppFrame::detach()
{
    if (mId != touchgfx::BITMAP_INVALID) {
        touchgfx::Bitmap::dynamicBitmapDelete(mId);
        mId = touchgfx::BITMAP_INVALID;
    }

    mImage.setBitmap(touchgfx::Bitmap(touchgfx::BITMAP_INVALID));
    mImage.setVisible(false);
    setVisible(false);
    mDrawnOwner = 0;
    mDrawnFrame = 0;
}

bool AppFrame::showFrame(uint32_t owner, uint32_t frame)
{
    if (mId == touchgfx::BITMAP_INVALID) {
        return false;
    }

    // The counter belongs to the producer and starts again from zero every
    // time the surface changes hands, so the same number from two of them is
    // two different frames. Only the pair says what is on screen.
    if (mImage.isVisible() && owner == mDrawnOwner && frame == mDrawnFrame) {
        return false;
    }

    mDrawnOwner = owner;
    mDrawnFrame = frame;
    mImage.setVisible(true);
    mImage.invalidate();

    return true;
}

bool AppFrame::hideFrame()
{
    if (mId == touchgfx::BITMAP_INVALID || !mImage.isVisible()) {
        return false;
    }

    // Hidden before it is invalidated, so the area is repainted from whatever
    // sits behind it.
    mImage.setVisible(false);
    mImage.invalidate();
    mDrawnOwner = 0;
    mDrawnFrame = 0;

    return true;
}
