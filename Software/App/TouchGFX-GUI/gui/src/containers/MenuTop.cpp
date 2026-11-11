#include <gui/containers/MenuTop.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <images/BitmapDatabase.hpp>
#include <touchgfx/Application.hpp>
#include <touchgfx/Bitmap.hpp>
#include <touchgfx/Font.hpp>

MenuTop::MenuTop()
{
    for (IconCarrier& carrier : mIcons) {
        carrier.icon.setBitmap(Bitmap(BITMAP_TOPMENU_DOT_ID));
        carrier.icon.setVisible(false);
        add(carrier.icon);
    }

    // The dot goes exactly where the selected item's icon would have rested, so
    // the selected position looks the same as it always has.
    auto [dotX, dotY] = coordForSlot(static_cast<float>(kSelectedSlot));
    mDot.setXY(dotX, dotY);
    mDot.setBitmap(Bitmap(BITMAP_TOPMENU_DOT_ID));
    mDot.setVisible(false);
    add(mDot);

    // Offset for 'icon' h/2 or w/2
    const int16_t offsetCenter = App::kIconWH / 2;
    mCenterIcon.setXY(getWidth() / 2 - offsetCenter, getHeight() / 2 - offsetCenter);
    mCenterIcon.setBitmap(Bitmap(BITMAP_TOPMENU_DOT_ID));
    mCenterIcon.setVisible(false);
    add(mCenterIcon);
}

MenuTop::~MenuTop()
{
    // Before releaseIcons(): a slide still registered as a timer widget would
    // be ticked after this container has gone.
    stopAnimation();
    releaseIcons();
}

void MenuTop::initialize()
{
    MenuTopBase::initialize();
}

void MenuTop::setNumberOfItems(int16_t numberOfItems)
{
    mItemCount = MIN(kMaxItems, numberOfItems);
    if (mItemCount < 0) {
        mItemCount = 0;
    }

    // A shorter list than last time can leave the cursor past the end; hold the
    // end of the list rather than point at an item that no longer exists.
    if (mCurrentId >= mItemCount) {
        mCurrentId = (mItemCount > 0) ? static_cast<int16_t>(mItemCount - 1) : 0;
    }

    // The list the strip was sliding over is not the list it would land in.
    stopAnimation();
    mScroll = static_cast<float>(mCurrentId);

    refresh();
}

int16_t MenuTop::getNumberOfItems()
{
    return mItemCount;
}

void MenuTop::config(int16_t id, const Unicode::UnicodeChar *name, BitmapId smallIcon, BitmapId centerIcon,
    const Unicode::UnicodeChar* nameAlt,  BitmapId smallIconAlt, BitmapId centerIconAlt)
{
    if (id < 0 || id >= mItemCount) {
        return;
    }

    mCtx[id].name = name;
    mCtx[id].smallId = smallIcon;
    mCtx[id].centerId = centerIcon;

    mCtx[id].nameAlt = nameAlt;
    mCtx[id].smalIdAlt = smallIconAlt;
    mCtx[id].centerIdAlt = centerIconAlt;

    // As configExternal() does. Items are reconfigured in place when the order
    // changes, so without this an item inherits whichever state the item that
    // used to be in this slot was left in.
    mCtx[id].useAlt = false;

    mCtx[id].ext30 = nullptr;
    mCtx[id].ext60 = nullptr;

    unbindItem(id);

    refresh();
}

void MenuTop::configExternal(int16_t id, const Unicode::UnicodeChar* name,
    const uint8_t* small30, const uint8_t* center60)
{
    if (id < 0 || id >= mItemCount) {
        return;
    }

    mCtx[id].name = name;
    mCtx[id].smallId = BITMAP_INVALID;
    mCtx[id].centerId = BITMAP_INVALID;

    mCtx[id].nameAlt = nullptr;
    mCtx[id].smalIdAlt = BITMAP_INVALID;
    mCtx[id].centerIdAlt = BITMAP_INVALID;
    mCtx[id].useAlt = false;

    mCtx[id].ext30 = small30;
    mCtx[id].ext60 = center60;

    unbindItem(id);

    refresh();
}

void MenuTop::unbindItem(int16_t id)
{
    // The item may already be carried, holding a bitmap made from the icons it
    // had before this call. A carrier's own bitmap takes precedence when
    // drawing, so leaving it bound would keep showing the old icon -- and, for
    // an external one, keep a descriptor pointing into a buffer the caller may
    // have released. Drop the binding so the next refresh rebuilds it.
    for (IconCarrier& carrier : mIcons) {
        if (carrier.itemId == id) {
            releaseCarrier(carrier);
        }
    }

    if (mCenterItemId == id) {
        freeCenterBitmap();
        mCenterItemId = -1;
    }
}

void MenuTop::releaseIcons()
{
    for (IconCarrier& carrier : mIcons) {
        releaseCarrier(carrier);
    }

    freeCenterBitmap();
    mCenterItemId = -1;
    mCenterIcon.setVisible(false);
    mCenterIcon.invalidate();

    // Nothing is selected once the icons have gone, so the mark for the
    // selected position goes with them.
    mDot.setVisible(false);
    mDot.invalidate();

    // Drop the external buffers too: they belong to an app list the caller is
    // free to release once it has asked us to let go of the icons.
    for (ItemContext& item : mCtx) {
        item.ext30 = nullptr;
        item.ext60 = nullptr;
    }
}

void MenuTop::setAltIcon(int16_t id, bool state)
{
    if (id >= 0 && id < mItemCount) {
        mCtx[id].useAlt = state;
        refresh();
    }
}

bool MenuTop::getAltIcon(int16_t id)
{
    if (id < 0 || id >= mItemCount) {
        return false;
    }

    return mCtx[id].useAlt;
}

void MenuTop::selectNext()
{
    // The list has ends. At the last item there is nowhere further to go, and
    // nothing to slide: the empty positions past it are the whole answer.
    if (mItemCount == 0 || mCurrentId >= mItemCount - 1) {
        return;
    }

    mCurrentId++;
    startAnimation();
    refresh();
}

void MenuTop::selectPrev()
{
    if (mItemCount == 0 || mCurrentId == 0) {
        return;
    }

    mCurrentId--;
    startAnimation();
    refresh();
}

void MenuTop::selectItem(int16_t id)
{
    if (mItemCount == 0) {
        return;
    }

    // A restored position can be out of range in either direction. There is no
    // wrap to fold it into, so hold the nearest end rather than index off the
    // end of the item table.
    mCurrentId = id;
    if (mCurrentId < 0) {
        mCurrentId = 0;
    } else if (mCurrentId >= mItemCount) {
        mCurrentId = static_cast<int16_t>(mItemCount - 1);
    }

    // Jump rather than slide: restoring a position is not the user moving.
    stopAnimation();
    mScroll = static_cast<float>(mCurrentId);

    refresh();
}

int16_t MenuTop::getSelectedItem() const
{
    return mCurrentId;
}

int16_t MenuTop::getIconWindow(int16_t *items, int16_t max) const
{
    if (items == nullptr || max <= 0 || mItemCount == 0) {
        return 0;
    }

    // One position further out than the arc rests at, either way: that is where
    // an item sits while it slides on or off, and it has to be drawable the
    // moment it appears there.
    int16_t first = static_cast<int16_t>(mCurrentId - kSelectedSlot - 1);
    int16_t last  = static_cast<int16_t>(mCurrentId - kSelectedSlot + kSlots);

    if (first < 0) {
        first = 0;
    }
    if (last > mItemCount - 1) {
        last = static_cast<int16_t>(mItemCount - 1);
    }

    int16_t count = 0;
    for (int16_t id = first; (id <= last) && (count < max); ++id) {
        items[count++] = id;
    }

    return count;
}

float MenuTop::slotOf(int16_t itemId) const
{
    return static_cast<float>(kSelectedSlot) + (static_cast<float>(itemId) - mScroll);
}

bool MenuTop::onArc(float slot)
{
    return (slot > -1.0f) && (slot < static_cast<float>(kSlots));
}

std::pair<int16_t, int16_t> MenuTop::coordForSlot(float slot) const
{
    // Offset for 'icon' h/2 or w/2
    const int16_t offsetSmall = App::kIconSmallWH / 2;
    const float cx = static_cast<float>(getWidth() / 2 - offsetSmall);
    const float cy = static_cast<float>(getHeight() / 2 - offsetSmall);

    const float angleDeg = kStartAngleDeg - slot * kStepAngleDeg;
    const float angleRad = angleDeg * PI / 180.0f;

    const float radius = static_cast<float>(kRadius);

    return { static_cast<int16_t>(cx + radius * std::sin(angleRad) + 0.5f),
             static_cast<int16_t>(cy - radius * std::cos(angleRad) + 0.5f) };
}

uint8_t MenuTop::alphaForSlot(float slot)
{
    // Full strength everywhere on the arc, so a resting strip looks exactly as
    // it always has. Only the step past either end fades, and that fade is the
    // whole of what carries an icon on and off the strip.
    const float last = static_cast<float>(kSlots - 1);

    float f = 1.0f;
    if (slot < 0.0f) {
        f = 1.0f + slot;                // 0 one step before the first position
    } else if (slot > last) {
        f = 1.0f - (slot - last);       // 0 one step past the last
    }

    if (f < 0.0f) {
        f = 0.0f;
    } else if (f > 1.0f) {
        f = 1.0f;
    }

    return static_cast<uint8_t>(f * 255.0f + 0.5f);
}

MenuTop::IconCarrier* MenuTop::carrierFor(int16_t itemId)
{
    for (IconCarrier& carrier : mIcons) {
        if (carrier.itemId == itemId) {
            return &carrier;
        }
    }

    return nullptr;
}

MenuTop::IconCarrier* MenuTop::freeCarrier()
{
    for (IconCarrier& carrier : mIcons) {
        if (carrier.itemId < 0) {
            return &carrier;
        }
    }

    return nullptr;
}

void MenuTop::bindCarrier(IconCarrier& carrier, int16_t itemId)
{
    releaseCarrier(carrier);
    carrier.itemId = itemId;

    if (mCtx[itemId].ext30 != nullptr) {
        carrier.dynId = Bitmap::dynamicBitmapCreateExternal(
            App::kIconSmallWH, App::kIconSmallWH, mCtx[itemId].ext30, Bitmap::ABGR2222);
    }
}

void MenuTop::releaseCarrier(IconCarrier& carrier)
{
    if (carrier.dynId != BITMAP_INVALID) {
        Bitmap::dynamicBitmapDelete(carrier.dynId);
        carrier.dynId = BITMAP_INVALID;
    }

    carrier.itemId = -1;

    // Hide as well as free: the widget outlives the bitmap, and an Image left
    // visible still holds the id it was last given. A freed id can be handed to
    // the next dynamic bitmap, so anything that drew before the next layout()
    // would draw whatever took its place.
    carrier.icon.setVisible(false);
    carrier.icon.invalidate();
}

void MenuTop::rebind()
{
    // Let go of a carrier whose item has left the arc, so it is free for one
    // that is arriving.
    for (IconCarrier& carrier : mIcons) {
        if (carrier.itemId < 0) {
            continue;
        }

        if (carrier.itemId >= mItemCount || !onArc(slotOf(carrier.itemId))) {
            releaseCarrier(carrier);
        }
    }

    for (int16_t id = 0; id < mItemCount; ++id) {
        if (!onArc(slotOf(id)) || carrierFor(id) != nullptr) {
            continue;
        }

        IconCarrier* carrier = freeCarrier();
        if (carrier == nullptr) {
            // Unreachable: the arc spans kSlots positions and the strip moves
            // by one, so at most kSlots + 1 items are ever on it at once.
            break;
        }

        bindCarrier(*carrier, id);
    }
}

void MenuTop::layout()
{
    for (IconCarrier& carrier : mIcons) {
        if (carrier.itemId < 0) {
            continue; // released carriers are hidden already
        }

        // At rest the selected item is not on the strip: the dot holds its
        // position and its icon is the enlarged one in the centre. While the
        // strip slides it is drawn like any other, so the strip stays whole.
        if (!mAnimating && carrier.itemId == mCurrentId) {
            carrier.icon.setVisible(false);
            continue;
        }

        const ItemContext& item = mCtx[carrier.itemId];

        // External icons have no alt variant, so the carrier's own bitmap wins.
        BitmapId id = carrier.dynId;
        if (id == BITMAP_INVALID) {
            id = (item.useAlt && item.smalIdAlt != BITMAP_INVALID) ? item.smalIdAlt : item.smallId;
        }

        if (id == BITMAP_INVALID) {
            carrier.icon.setVisible(false);
            continue;
        }

        const float slot = slotOf(carrier.itemId);
        auto [x, y] = coordForSlot(slot);
        carrier.icon.setBitmap(Bitmap(id));
        carrier.icon.setXY(x, y);
        carrier.icon.setAlpha(alphaForSlot(slot));
        carrier.icon.setVisible(true);
    }

    // Hidden while the strip moves: an icon is passing through the selected
    // position on its way in, and two marks for one position read as a fault.
    mDot.setVisible(!mAnimating && mItemCount > 0);
}

void MenuTop::refresh()
{
    // Hide all
    if (mItemCount == 0) {
        for (IconCarrier& carrier : mIcons) {
            releaseCarrier(carrier);
        }

        freeCenterBitmap();
        mCenterItemId = -1;
        mCenterIcon.setVisible(false);
        mDot.setVisible(false);

        text.setVisible(false);

        invalidate();
        return;
    }

    rebind();
    layout();

    bindCenter(mCurrentId);
    refreshCenter();

    invalidate();
}

void MenuTop::bindCenter(int16_t itemId)
{
    if (mCenterItemId == itemId) {
        return;
    }

    freeCenterBitmap();
    mCenterItemId = itemId;

    if (mCtx[itemId].ext60 != nullptr) {
        mCenterDynId = Bitmap::dynamicBitmapCreateExternal(
            App::kIconWH, App::kIconWH, mCtx[itemId].ext60, Bitmap::ABGR2222);
    }
}

void MenuTop::freeCenterBitmap()
{
    if (mCenterDynId != BITMAP_INVALID) {
        Bitmap::dynamicBitmapDelete(mCenterDynId);
        mCenterDynId = BITMAP_INVALID;
    }
}

void MenuTop::refreshCenter()
{
    const ItemContext& item = mCtx[mCurrentId];

    // Central item selected
    BitmapId id = mCenterDynId;
    if (id == BITMAP_INVALID) {
        id = (item.useAlt && item.centerIdAlt != BITMAP_INVALID) ? item.centerIdAlt : item.centerId;
    }

    if (id != BITMAP_INVALID) {
        mCenterIcon.setBitmap(Bitmap(id));
        mCenterIcon.setVisible(true);
    }
    else {
        mCenterIcon.setVisible(false);
    }

    // Text
    const Unicode::UnicodeChar* name =
        (item.useAlt && item.nameAlt != nullptr) ? item.nameAlt : item.name;

    if (name != nullptr) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", name);
        text.setTypedText(typedTextForLabel(textBuffer));
        text.setVisible(true);
    }
    else {
        text.setVisible(false);
    }
}

touchgfx::TypedText MenuTop::typedTextForLabel(const Unicode::UnicodeChar *str) const
{
    // Not "small": Windows' rpcndr.h, which the simulator pulls in, defines
    // that as a macro for char.
    const touchgfx::TypedText larger(T_TMP_SEMIBOLD_20);
    const touchgfx::TypedText smaller(T_TMP_SEMIBOLD_18);

    const touchgfx::Font *font = larger.getFont();
    if (font == nullptr) {
        return larger;
    }

    // Measured through the typed text's own wildcard, exactly as the draw path
    // does, and not by handing the name in as the format string: getStringWidth
    // is variadic and scans what it is given for wildcards, so a name carrying
    // a U+0002 -- and an app's name is whatever its .uapp says -- would have it
    // read an argument that was never passed.
    //
    // getWidth() rather than a constant: the box is the Designer's to move.
    if (font->getStringWidth(larger.getTextDirection(), larger.getText(), str)
            > text.getWidth()) {
        return smaller;
    }

    return larger;
}

void MenuTop::startAnimation()
{
    // A slide re-targeted before it finishes eases on from wherever the strip
    // has got to, and at one step per frame that settles three items behind the
    // selection: the centre icon and the label run ahead of the arc, and the
    // trailing items are on the arc but outside the window their icons were
    // asked for, so they slide past blank. Give up the backlog rather than
    // carry it -- the strip stays within a step of the selection and travels
    // at the rate being asked of it.
    const float target = static_cast<float>(mCurrentId);

    if (target - mScroll > kMaxLag) {
        mScroll = target - kMaxLag;
    } else if (mScroll - target > kMaxLag) {
        mScroll = target + kMaxLag;
    }

    mAnimFrom = mScroll;
    mAnimStep = 0;

    if (!mAnimating) {
        touchgfx::Application::getInstance()->registerTimerWidget(this);
        mAnimating = true;
    }
}

void MenuTop::stopAnimation()
{
    if (mAnimating) {
        touchgfx::Application::getInstance()->unregisterTimerWidget(this);
        mAnimating = false;
    }

    mAnimStep = 0;
}

void MenuTop::handleTickEvent()
{
    if (!mAnimating) {
        return;
    }

    mAnimStep++;

    // Linear, so the strip covers equal ground each frame. There are only a
    // handful of frames in a step; easing them would read as a stutter rather
    // than as ease.
    const float target = static_cast<float>(mCurrentId);
    const float f = static_cast<float>(mAnimStep) / static_cast<float>(kAnimSteps);
    mScroll = mAnimFrom + (target - mAnimFrom) * f;

    if (mAnimStep >= kAnimSteps) {
        mScroll = target;
        // Before the refresh below, so that frame is the one where the arriving
        // icon gives its position back to the dot.
        stopAnimation();
    }

    refresh();
}
