#include <gui/containers/WatchFaceChrome.hpp>
#include <gui/common/GuiConfig.hpp>

/// How long the chrome stays up after the last thing worth showing it for.
static constexpr uint16_t kVisibleTicks = GUI_CONFIG_MS_2_TICKS(3000);

WatchFaceChrome::WatchFaceChrome()
    : mHideCb(this, &WatchFaceChrome::onHide)
{

}

void WatchFaceChrome::initialize()
{
    WatchFaceChromeBase::initialize();

    scrollIndicator.setConfig(ScrollIndicator::kSmall);

    // Read from the config the rail itself is set from, so the backing and what
    // sits on it cannot drift apart. The angles held in the Designer are only
    // what the layout is drawn with.
    scrollIndicatorBorder.setArc(ScrollIndicator::kSmall.railMin,
                                 ScrollIndicator::kSmall.railMax);

    mHideTimer.setDuration(kVisibleTicks);
    mHideTimer.setCallback(mHideCb);
}

void WatchFaceChrome::show()
{
    if (!isVisible()) {
        setVisible(true);
        invalidate();
    }

    mHideTimer.start();
}

void WatchFaceChrome::onHide()
{
    // Invalidated while still visible, so the area is repainted from the face
    // behind it rather than left holding the chrome.
    invalidate();
    setVisible(false);
}

void WatchFaceChrome::setCursor(uint8_t count, uint8_t index)
{
    scrollIndicator.setCount(count);
    scrollIndicator.setActiveId(index);
}

void WatchFaceChrome::stepCursor(int8_t delta)
{
    // Counted from where the handle is rather than from the position stepped
    // to, so a step off either end reads as one step and slides round, instead
    // of running the whole rail back.
    scrollIndicator.animateToId(static_cast<int16_t>(scrollIndicator.getActiveId()) + delta,
                                Gui::Config::kMenuAnimationSteps);
}
