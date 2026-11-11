#ifndef SCROLLINGTEXT_HPP
#define SCROLLINGTEXT_HPP

#include <gui_generated/containers/ScrollingTextBase.hpp>

/**
 * Auto-scrolling (marquee) text. Shows the text centered when it fits the
 * widget's fixed width; when it does not, marquees it left after a pause in a
 * seamless circular loop (a trailing copy follows one period behind, so it never
 * jumps back to the start). The single line is vertically centered. Size is fixed
 * by the design (Designer-set width/height), not derived from the text. Overflow
 * is clipped by the container. Configure via setText / setColor / setTypedText /
 * setScrollSpeed / setScrollPauseTicks.
 */
class ScrollingText : public ScrollingTextBase
{
public:
    ScrollingText();
    virtual ~ScrollingText();

    virtual void initialize();
    virtual void handleTickEvent() override;

    /** Set the UTF-8 text; re-centers it, or arms the marquee if it overflows. */
    void setText(const char* utf8);

    /** Text color. */
    void setColor(touchgfx::colortype color);

    /** Text font/typography. Call before setText (it re-measures on setText). */
    void setTypedText(const touchgfx::TypedText& typedText);

    /** Marquee speed in pixels per tick (0 is treated as 1). */
    void setScrollSpeed(uint8_t pixelsPerTick);

    /** Pause before the marquee starts, in ticks. The caller converts from time,
     *  so the widget stays free of the GUI tick rate. */
    void setScrollPauseTicks(uint16_t ticks);

private:
    // STATIC: fits, centered, no ticks. Otherwise PAUSE_START (initial delay) ->
    // SCROLLING (continuous, seamless circular loop via the trailing copy).
    enum class Phase : uint8_t { STATIC, PAUSE_START, SCROLLING };

    static const uint8_t kDefaultSpeed = 1;
    static const uint16_t kDefaultPauseTicks = 30;   ///< Fallback; the screen sets it.
    static const int16_t kGapPx = 40;   ///< Blank gap between the looped copies.

    // Second copy of the text, kept one period (text width + gap) behind the
    // primary line so the wrap is seamless (no jump back to the start).
    touchgfx::TextAreaWithOneWildcard mTrail;

    Phase mPhase { Phase::STATIC };
    int16_t mOffset { 0 };          ///< Current left shift (px) while scrolling.
    int16_t mPeriod { 0 };          ///< Wrap period = text width + gap.
    uint16_t mTextWidth { 0 };      ///< Pixel width of the current text.
    uint16_t mPauseTicks { 0 };     ///< Pause length in ticks.
    uint16_t mPauseCounter { 0 };   ///< Ticks left in the current pause.
    uint8_t mSpeed { kDefaultSpeed };
    bool mRegistered { false };     ///< Whether we receive tick events.

    void layout();          ///< Measure + center-or-arm-scroll + vertical center.
    void registerTick();
    void unregisterTick();
};

#endif // SCROLLINGTEXT_HPP
