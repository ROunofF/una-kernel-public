/**
 ******************************************************************************
 * @file    ILcd.hpp
 * @brief   LCD Interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_LCD_HPP
#define __INTERFACE_I_LCD_HPP

#include <cstdint>
#include <cstdbool>

namespace Interface
{

/**
 * @brief   LCD Interface.
 */
class ILcd {

public:

    enum class Color : uint8_t {
        WHITE = 0xFF,
        BLACK = 0x00,
        RED   = (3 << 0),
        GREEN = (3 << 2),
        BLUE  = (3 << 4),
    };

    /**
     * @brief   Initialize LCD object.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief   Deinitialize LCD object and free all resources.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() = 0;

    /**
     * @brief   Enable/Turn on LCD.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool enable() = 0;

    /**
     * @brief   Disable/Turn off LCD or put it to low power mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool disable() = 0;

    /**
     * @brief   Check if the LCD is on.
     * @retval  'true' - LCD is on, 'false' - otherwise.
     */
    virtual bool isEnabled() = 0;

    /**
     * @brief   Configure LCD resolution.
     * @note    The driver may have a fixed resolution. Therefore, this method
     *          can be ignored. To verify the resolution, use the
     *          getResolution(uint32_t &width, uint32_t &height) method.
     * @param   width: LCD width.
     * @param   height: LCD height.
     */
    virtual void setResolution(int16_t width, int16_t height) = 0;

    /**
     * @brief   Get LCD resolution.
     * @param   width: LCD width.
     * @param   height: LCD height.
     */
    virtual void getResolution(int16_t &width, int16_t &height) = 0;

    /**
     * @brief Retrieves the display color depth.
     * @return Bits per pixel.
     */
    virtual uint8_t getDisplayColorDepth() = 0;

    /**
     * @brief   Write data to LCD.
     * @param   pBuf: Frame buffer.
     */
    virtual void write(const uint8_t *pBuf) = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~ILcd() = default;

};

} /* namespace Interface */

#endif /* __INTERFACE_I_LCD_HPP */
