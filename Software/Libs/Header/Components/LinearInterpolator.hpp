#pragma once

namespace Tools {

class LinearInterpolator{
public:
    static constexpr float Epsilon = 1e-6f;

    constexpr LinearInterpolator() = default;

    constexpr LinearInterpolator(float x1, float x2, float y1, float y2)
    {
        init(x1, x2, y1, y2);
    }

    constexpr bool init(float x1, float x2, float y1, float y2)
    {
        const float dx = x2 - x1;
        if ((dx > -Epsilon) && (dx < Epsilon)) {
            return false;
        }

        mX1 = x1;
        mX2 = x2;
        mY1 = y1;
        mY2 = y2;
        mSlope  = (y2 - y1) / dx;
        mOffset = y1 - mSlope * x1;

        return true;
    }

    [[nodiscard]] constexpr float f(float x) const
    {
        if (x < mX1) {
            x = mX1;
        } else if (x > mX2) {
            x = mX2;
        }

        return execute(x);
    }

    [[nodiscard]] constexpr float execute(float x) const
    {
        return x * mSlope + mOffset;
    }

    [[nodiscard]] constexpr float getSlope() const
    {
        return mSlope;
    }

    [[nodiscard]] constexpr float getOffset() const
    {
        return mOffset;
    }

    [[nodiscard]] constexpr float getX1() const
    {
        return mX1;
    }

    [[nodiscard]] constexpr float getX2() const
    {
        return mX2;
    }

    [[nodiscard]] constexpr float getY1() const
    {
        return mY1;
    }

    [[nodiscard]] constexpr float getY2() const
    {
        return mY2;
    }

private:
    float mSlope  = 0.0f;
    float mOffset = 0.0f;
    float mX1     = 0.0f;
    float mX2     = 0.0f;
    float mY1     = 0.0f;
    float mY2     = 0.0f;
};

} // namespace Tools
