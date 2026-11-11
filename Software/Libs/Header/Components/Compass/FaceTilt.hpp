/**
 ******************************************************************************
 * @file    FaceTilt.hpp
 * @brief   How far the face is turned from facing up, from gravity.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_FACE_TILT_HPP
#define COMPONENT_FACE_TILT_HPP

#include <cmath>

/**
 * @brief   The angle between the face and the sky.
 *
 *          A bearing exists however the face is turned; whether it can be read
 *          off a rose drawn on that face is another matter, and this is what
 *          it takes to decide it.
 *
 *          Axes are the watch's own: +Z out of the face.
 */
class FaceTilt
{
public:
    /**
     * @brief   How far the face is turned from facing straight up.
     * @param   gx, gy, gz: The accelerometer's reading in watch axes, face up
     *          at rest reading gz positive. Any unit: only its direction is
     *          used.
     * @retval  Degrees in [0, 180]: 0 face up, 90 on edge, 180 face down.
     *          180 as well for gravity that is not a direction.
     */
    static float degrees(float gx, float gy, float gz)
    {
        constexpr float RAD_TO_DEG = 57.29577951308232f;

        const float magnitude = std::sqrt((gx * gx) + (gy * gy) + (gz * gz));
        const float nz        = gz / magnitude;

        if (nz >= 1.0f) {
            return 0.0f;
        }

        // A NaN fails this and the test above, and lands on the far end.
        if (!(nz > -1.0f)) {
            return 180.0f;
        }

        return std::acos(nz) * RAD_TO_DEG;
    }
};

#endif // COMPONENT_FACE_TILT_HPP
