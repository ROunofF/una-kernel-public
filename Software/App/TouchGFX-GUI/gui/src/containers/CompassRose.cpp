#include <gui/containers/CompassRose.hpp>

#include <math.h>

namespace {
const float kDegToRad = 3.14159265f / 180.0f;
} // namespace

CompassRose::CompassRose()
{
    // Read while the parts are still where the Designer put them: every
    // offset below is measured against that placement.
    capture(mMarks[0].label, north);
    capture(mMarks[1].label, east);
    capture(mMarks[2].label, south);
    capture(mMarks[3].label, west);

    capture(mMarks[0].gap, gapNorth);
    capture(mMarks[1].gap, gapEast);
    capture(mMarks[2].gap, gapSouth);
    capture(mMarks[3].gap, gapWest);

    for (unsigned i = 0; i < kMarks; i++) {
        mPivotX += mMarks[i].label.dx;
        mPivotY += mMarks[i].label.dy;
    }

    mPivotX /= static_cast<float>(kMarks);
    mPivotY /= static_cast<float>(kMarks);

    for (unsigned i = 0; i < kMarks; i++) {
        mMarks[i].label.dx -= mPivotX;
        mMarks[i].label.dy -= mPivotY;
        mMarks[i].gap.dx   -= mPivotX;
        mMarks[i].gap.dy   -= mPivotY;
    }
}

void CompassRose::initialize()
{
    CompassRoseBase::initialize();

    setHeading(0.0f);
}

void CompassRose::capture(Part &part, touchgfx::Drawable &widget)
{
    part.widget = &widget;
    part.dx = static_cast<float>(widget.getX()) + (widget.getWidth() / 2.0f);
    part.dy = static_cast<float>(widget.getY()) + (widget.getHeight() / 2.0f);
}

void CompassRose::place(const Part &part, float pivotX, float pivotY,
        float cosA, float sinA)
{
    const float cx = pivotX + (part.dx * cosA) - (part.dy * sinA);
    const float cy = pivotY + (part.dx * sinA) + (part.dy * cosA);

    const int16_t x = static_cast<int16_t>(
            lroundf(cx - (part.widget->getWidth() / 2.0f)));
    const int16_t y = static_cast<int16_t>(
            lroundf(cy - (part.widget->getHeight() / 2.0f)));

    // A bearing that has not moved a whole pixel repaints nothing.
    if ((x == part.widget->getX()) && (y == part.widget->getY())) {
        return;
    }

    part.widget->moveTo(x, y);
}

void CompassRose::setSweep(float degrees)
{
    ring.setArc<float>(0.0f, degrees);

    // The whole widget: a shrunk sweep leaves pixels outside the new arc.
    ring.invalidate();
}

void CompassRose::showMarks(bool show)
{
    for (unsigned i = 0; i < kMarks; i++) {
        mMarks[i].gap.widget->setVisible(show);
        mMarks[i].gap.widget->invalidate();

        mMarks[i].label.widget->setVisible(show);
        mMarks[i].label.widget->invalidate();
    }
}

void CompassRose::setHeading(float degrees)
{
    // North sits opposite the way the watch points, so the rose turns against
    // the bearing.
    const float a = -degrees * kDegToRad;
    const float c = cosf(a);
    const float s = sinf(a);

    for (unsigned i = 0; i < kMarks; i++) {
        place(mMarks[i].gap, mPivotX, mPivotY, c, s);
        place(mMarks[i].label, mPivotX, mPivotY, c, s);
    }
}
