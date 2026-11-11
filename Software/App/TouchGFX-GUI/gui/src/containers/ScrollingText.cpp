#include <gui/containers/ScrollingText.hpp>

#include <touchgfx/Application.hpp>
#include <touchgfx/Unicode.hpp>

ScrollingText::ScrollingText()
{
    mPauseTicks = kDefaultPauseTicks;

    // Single-line marquee: never wrap or ellipsize; the container clips overflow
    // and this widget scrolls it instead (the generated base defaults to wrap).
    text.setWideTextAction(WIDE_TEXT_NONE);

    // Trailing copy for the seamless circular loop: same buffer, color and font
    // as the primary line; hidden until the text overflows.
    mTrail.setWildcard(textBuffer);
    mTrail.setWideTextAction(WIDE_TEXT_NONE);
    mTrail.setColor(text.getColor());
    mTrail.setTypedText(text.getTypedText());
    mTrail.setVisible(false);
    add(mTrail);
}

ScrollingText::~ScrollingText()
{
    unregisterTick();
}

void ScrollingText::initialize()
{
    ScrollingTextBase::initialize();
}

void ScrollingText::setColor(touchgfx::colortype color)
{
    text.setColor(color);
    mTrail.setColor(color);
    invalidate();
}

void ScrollingText::setTypedText(const touchgfx::TypedText& typedText)
{
    text.setTypedText(typedText);
    mTrail.setTypedText(typedText);
}

void ScrollingText::setScrollSpeed(uint8_t pixelsPerTick)
{
    mSpeed = (pixelsPerTick == 0) ? 1 : pixelsPerTick;
}

void ScrollingText::setScrollPauseTicks(uint16_t ticks)
{
    mPauseTicks = ticks;
}

void ScrollingText::setText(const char* utf8)
{
    touchgfx::Unicode::UnicodeChar next[TEXT_SIZE];
    touchgfx::Unicode::fromUTF8(
            reinterpret_cast<const uint8_t*>(utf8 != nullptr ? utf8 : ""),
            next, TEXT_SIZE);
    // fromUTF8 does not append a terminator when the source fills the buffer, so
    // a too-long title would render past the array as garbage. Force a terminator.
    next[TEXT_SIZE - 1] = 0;

    // Unchanged text keeps the current scroll running instead of restarting it.
    // Now Playing updates arrive on every field change (progress, state, ...),
    // usually repeating the title; only a real title change should reset scroll.
    if (touchgfx::Unicode::strncmp(next, textBuffer, TEXT_SIZE) == 0) {
        return;
    }

    touchgfx::Unicode::strncpy(textBuffer, next, TEXT_SIZE);
    layout();
}

void ScrollingText::layout()
{
    const int16_t w = getWidth();
    const int16_t h = getHeight();

    mTextWidth = text.getTextWidth();

    // Vertically center the single line within the fixed height.
    const touchgfx::Font* font = text.getTypedText().getFont();
    const int16_t lineH = (font != nullptr) ? font->getHeight() : h;
    const int16_t ty = (h - lineH) / 2;

    if (mTextWidth <= w) {
        // Fits: static, horizontally centered by offset (left-aligned text in a
        // text-width box placed at the center); no trailing copy.
        unregisterTick();
        mPhase = Phase::STATIC;
        mOffset = 0;
        mTrail.setVisible(false);
        text.setPosition((w - mTextWidth) / 2, ty, mTextWidth, lineH);
    } else {
        // Overflows: park at the left edge and marquee after a pause. The trailing
        // copy sits one period (text width + gap) to the right so that, once the
        // primary line scrolls off, the loop continues seamlessly.
        mPeriod = static_cast<int16_t>(mTextWidth + kGapPx);
        mPhase = Phase::PAUSE_START;
        mPauseCounter = mPauseTicks;
        mOffset = 0;
        text.setPosition(0, ty, mTextWidth, lineH);
        mTrail.setPosition(mPeriod, ty, mTextWidth, lineH);
        mTrail.setVisible(true);
        registerTick();
    }
    invalidate();
}

void ScrollingText::handleTickEvent()
{
    switch (mPhase) {
        case Phase::PAUSE_START:
            if (mPauseCounter == 0 || --mPauseCounter == 0) {
                mPhase = Phase::SCROLLING;
            }
            break;

        case Phase::SCROLLING:
            mOffset += mSpeed;
            if (mOffset >= mPeriod) {
                // Seamless wrap: the trailing copy is now exactly where the
                // primary line restarts, so nothing visibly jumps.
                mOffset -= mPeriod;
            }
            text.setX(-mOffset);
            mTrail.setX(mPeriod - mOffset);
            invalidate();
            break;

        case Phase::STATIC:
        default:
            break;
    }
}

void ScrollingText::registerTick()
{
    if (!mRegistered) {
        touchgfx::Application::getInstance()->registerTimerWidget(this);
        mRegistered = true;
    }
}

void ScrollingText::unregisterTick()
{
    if (mRegistered) {
        touchgfx::Application::getInstance()->unregisterTimerWidget(this);
        mRegistered = false;
    }
}
