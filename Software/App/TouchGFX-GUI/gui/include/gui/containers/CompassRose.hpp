#ifndef COMPASSROSE_HPP
#define COMPASSROSE_HPP

#include <gui_generated/containers/CompassRoseBase.hpp>

/**
 * The four cardinal marks, turned so that north keeps pointing north while the
 * watch moves under them.
 *
 * A disc in the background colour rides under each letter and cuts its gap in
 * the ring, so the gaps travel with the marks and the ring never moves. The
 * disc is deliberately wider than the gap: it keeps the letter legible while
 * the rose turns across whatever the screen has placed outside the ring.
 *
 * The ring can be drawn short of a full turn to show a calibration filling
 * it; a dimmer, always whole circle beneath it shows what it is a fraction of.
 *
 * Where the marks sit is a Designer question: construction records each part's
 * offset from a pivot, the mean of the four label centres, so the four must
 * stay symmetric about the ring's centre.
 *
 * The glyphs stay upright: a TextArea can only be rotated in quarter turns.
 */
class CompassRose : public CompassRoseBase
{
public:
    CompassRose();
    virtual ~CompassRose() {}

    virtual void initialize();

    /**
     * Turn the rose so that north lies at the given bearing.
     *
     * @param degrees Where the top of the watch points, clockwise from north.
     */
    void setHeading(float degrees);

    /**
     * How much of the ring is drawn, clockwise from the top.
     *
     * @param degrees A whole 360 is the rose at rest; less is a ring being
     *        filled. The caller hides the marks itself.
     */
    void setSweep(float degrees);

    /**
     * Show or hide the marks, leaving the ring drawn.
     */
    void showMarks(bool show);

private:
    /// One widget and where it was placed, as an offset from the pivot.
    struct Part {
        touchgfx::Drawable *widget;
        float               dx;
        float               dy;
    };

    /// A letter and the hole in the ring it sits in.
    struct Mark {
        Part label;
        Part gap;
    };

    static const unsigned kMarks = 4;

    void        capture(Part &part, touchgfx::Drawable &widget);
    static void place(const Part &part, float pivotX, float pivotY,
                      float cosA, float sinA);

    Mark  mMarks[kMarks];

    // Mean of the four letter centres; see the class comment.
    float mPivotX { 0.0f };
    float mPivotY { 0.0f };
};

#endif // COMPASSROSE_HPP
