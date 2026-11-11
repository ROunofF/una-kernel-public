#ifndef MENUTOP_HPP
#define MENUTOP_HPP

#include <gui_generated/containers/MenuTopBase.hpp>
#include <touchgfx/hal/Types.hpp>
#include <touchgfx/TypedText.hpp>
#include <touchgfx/widgets/Image.hpp>
#include <array>
#include <cmath>
#include <utility>

/**
 * @brief The top menu wheel: small icons on an arc, the selected one enlarged
 *        in the centre with its label.
 *
 * The arc has kSlots resting positions at fixed coordinates. The selection is
 * one of them -- kSelectedSlot -- and it never moves: selecting slides the icon
 * strip along the arc past it instead. The item that arrives there leaves the
 * strip, a dot holds the position it settles on, and its icon is the enlarged
 * one in the centre with its label.
 *
 * kSelectedSlot is 3, so the selection sits at 9 o'clock. The three fixed
 * entries keep their order and their spacing -- Settings selected, Power off
 * one step above it, Alerts one below -- which means that at rest, with
 * nothing before Power off, the two positions above it are empty.
 *
 * The list has ends -- there is no wrap in either direction. The positions
 * before the first item and after the last are simply empty, which is what
 * tells the user an end has been reached.
 *
 * Item indices are the caller's; positions are internal. config()/selectItem()/
 * getSelectedItem() all speak item indices regardless of where the strip is.
 */
class MenuTop : public MenuTopBase
{
public:
    MenuTop();
    virtual ~MenuTop();

    /// The carriers own dynamic bitmaps that the destructor frees, and the
    /// TouchGFX base classes are copyable, so a copy would free them twice.
    /// Nothing copies a container; this makes that a compile error, not a
    /// double free.
    MenuTop(const MenuTop&) = delete;
    MenuTop& operator=(const MenuTop&) = delete;
    MenuTop(MenuTop&&) = delete;
    MenuTop& operator=(MenuTop&&) = delete;

    virtual void initialize();

    /// Resting positions on the arc: seven, every 30 degrees from 12 o'clock
    /// counter-clockwise to 6 o'clock.
    static constexpr int16_t kSlots = 7;

    /// The resting position the selection occupies, and never leaves -- the
    /// fourth, which the step above puts at 9 o'clock.
    static constexpr int16_t kSelectedSlot = 3;

    /**
     * @brief Item capacity.
     *
     * Decoupled from kSlots, and from the apps' icons: what is on the arc costs
     * a widget and a dynamic bitmap, and the icons themselves are asked for a
     * window at a time (see getIconWindow()), so an item on this list costs
     * only its name and its place. That is what makes a hundred affordable.
     */
    static constexpr int16_t kMaxItems = 100;

    void setNumberOfItems(int16_t numberOfItems);
    int16_t getNumberOfItems();

    /** @brief Configure an item whose icons come from the bitmap database. */
    void config(int16_t id, const Unicode::UnicodeChar* name, BitmapId smallIcon, BitmapId centerIcon,
        const Unicode::UnicodeChar* nameAlt = nullptr,
        BitmapId smallIconAlt = BITMAP_INVALID, BitmapId centerIconAlt = BITMAP_INVALID);

    /**
     * @brief Configure an item whose icons are raw external ABGR2222 pixels.
     *
     * For user apps, whose icons arrive as buffers in the app list rather than
     * as database entries. The dynamic bitmaps are created and released as the
     * strip carries the item on and off the arc, so the cost is what is on
     * screen, not the number of items. @p small30 and @p center60 must outlive
     * the menu's use of them -- they point into the caller's App::Item list.
     */
    void configExternal(int16_t id, const Unicode::UnicodeChar* name,
        const uint8_t* small30, const uint8_t* center60);

    /**
     * @brief Release every dynamic bitmap the menu owns.
     *
     * Call when leaving the screen, and before repointing items at a new app
     * list: it also drops the external pixel pointers so nothing can be
     * recreated from a buffer the caller has released.
     */
    void releaseIcons();

    void setAltIcon(int16_t id, bool state);
    bool getAltIcon(int16_t id);

    void selectNext();
    void selectPrev();
    void selectItem(int16_t id);
    int16_t getSelectedItem() const;

    /**
     * @brief The items whose icons the arc needs, or is about to.
     *
     * The resting positions plus the one item beyond each end -- the one
     * sliding on or off while the strip moves -- so an icon is in hand by the
     * time it is drawn. Item indices, in ascending order; the caller turns them
     * into whatever its own icons are keyed by.
     *
     * @param  items: filled with item indices.
     * @param  max: capacity of @p items.
     * @retval How many were written.
     */
    int16_t getIconWindow(int16_t *items, int16_t max) const;


protected:

    virtual void handleTickEvent() override;

    /// Icon widgets. One more than there are resting positions: while the strip
    /// slides, an icon is entering the arc at one end before the one leaving at
    /// the other has finished going.
    static constexpr int16_t kIcons = kSlots + 1;

    /// Steps a slide takes, one per tick. Matches the rest of the GUI's menus.
    static constexpr int16_t kAnimSteps = 4;

    /// How far the strip may trail the selection. Steps arriving faster than a
    /// slide completes would otherwise let a backlog build -- see
    /// startAnimation() -- and the items dragged along behind it are on the arc
    /// without being in the window their icons were asked for.
    static constexpr float kMaxLag = 1.0f;

    // The arc. coordForSlot() subtracts slot * kStepAngleDeg from the start, so
    // a positive step runs counter-clockwise: slot 0 at 12 o'clock, slot 3 at 9
    // o'clock, slot 6 at 6 o'clock. A fractional position is interpolated along
    // the same circle, which is what keeps a sliding icon on the arc rather
    // than cutting the chord between two positions.
    static constexpr int16_t kRadius        = 100;
    static constexpr float   kStartAngleDeg = 0.0f;
    static constexpr float   kStepAngleDeg  = 30.0f;

    int16_t mItemCount = 0;
    int16_t mCurrentId = 0;

    /// Fractional selected index -- the strip's position along the arc. Equal
    /// to mCurrentId at rest, and eased toward it while sliding.
    float mScroll = 0.0f;

    float   mAnimFrom  = 0.0f;  ///< mScroll when the slide started.
    int16_t mAnimStep  = 0;     ///< Steps taken, 0 .. kAnimSteps.
    bool    mAnimating = false;

    /// One item's content. Icons are either database ids or external pixels.
    struct ItemContext {
        const Unicode::UnicodeChar *name = nullptr;
        BitmapId smallId = BITMAP_INVALID;
        BitmapId centerId = BITMAP_INVALID;

        const Unicode::UnicodeChar* nameAlt = nullptr;
        BitmapId smalIdAlt = BITMAP_INVALID;
        BitmapId centerIdAlt = BITMAP_INVALID;

        bool useAlt = false;

        const uint8_t* ext30 = nullptr;  ///< External small icon; the carrier owns its bitmap.
        const uint8_t* ext60 = nullptr;  ///< External centre icon; the centre owns its bitmap.
    };

    /// One icon on the strip. Carries whichever item is on the arc, wherever on
    /// it that item currently is; recycled as items come and go.
    struct IconCarrier {
        Image icon {};
        int16_t itemId = -1;              ///< Item currently carried, -1 if free.
        BitmapId dynId = BITMAP_INVALID;  ///< Owned dynamic bitmap, if the item is external.
    };

    std::array<ItemContext, kMaxItems> mCtx {};
    std::array<IconCarrier, kIcons> mIcons {};

    Image mDot {};                         ///< Marks kSelectedSlot. Never moves.

    Image mCenterIcon {};
    int16_t mCenterItemId = -1;            ///< Item the centre icon is bound to.
    BitmapId mCenterDynId = BITMAP_INVALID;///< Owned dynamic bitmap, if that item is external.

    /// Where on the arc an item currently sits. kSelectedSlot for the selection.
    float slotOf(int16_t itemId) const;
    /// Whether a position is on the arc at all. The ends are exclusive, so an
    /// icon that has slid a whole step past the last position has gone.
    static bool onArc(float slot);
    /// Top-left of a 30 px icon at a (possibly fractional) arc position.
    std::pair<int16_t, int16_t> coordForSlot(float slot) const;
    /// Icon opacity at a position: full across the arc, fading away over the
    /// step past either end. Nothing else hides an icon joining or leaving the
    /// strip, so this fade is what stops one blinking in and out.
    static uint8_t alphaForSlot(float slot);

    void unbindItem(int16_t id);  ///< Drop any carrier/centre binding for an item.
    void rebind();                ///< Give every item on the arc a carrier, and free the rest.
    void layout();                ///< Place and show every carrier for the current mScroll.
    void refresh();               ///< Rebind, lay out, update the centre, redraw.
    void refreshCenter();

    IconCarrier* carrierFor(int16_t itemId);
    IconCarrier* freeCarrier();
    void bindCarrier(IconCarrier& carrier, int16_t itemId);
    void releaseCarrier(IconCarrier& carrier);

    void bindCenter(int16_t itemId);
    void freeCenterBitmap();

    /**
     * @brief The largest of the label's two sizes that @p str fits on one line
     *        in, or the smaller one if it fits in neither.
     *
     * App names come out of the .uapp and run to App::kNameLen - 1 characters,
     * which is wider than the label at the larger size. The label is centred,
     * so overrunning it loses characters from both ends rather than just the
     * tail -- "Sleep Analysis!" drew as "eep Analysi". Dropping a size covers
     * everything up to the equivalent of 134 px at the larger size; past that
     * the widget's wide-text action ellipsises, which at least keeps the start
     * of the name.
     */
    touchgfx::TypedText typedTextForLabel(const Unicode::UnicodeChar *str) const;

    void startAnimation();
    void stopAnimation();

};

#endif // MENUTOP_HPP
