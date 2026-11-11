/**
 ******************************************************************************
 * @file    LS012B7DD06A.hpp
 * @brief   SHARP %LS012B7DD06A LCD.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __HARDWARE_LS012B7DD06A_HPP
#define __HARDWARE_LS012B7DD06A_HPP

#include <cstdint>
#include <cstdbool>

#include "main.h"
#include "IGpio.hpp"
#include "ILcd.hpp"

namespace Hardware
{

/**
 * @brief   SHARP %LS012B7DD06A LCD.
 */
class LS012B7DD06A : virtual public Interface::ILcd
{

public:

    /**
     * @brief   Constructor.
     */
    LS012B7DD06A() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~LS012B7DD06A()
    {
        deinit();
    }

    /**
     * @brief   Configure with hardware resources.
     * @param   vst: VST  / GSP pin. Start signal for the vertical driver
     * @param   vck: VCK  / GCK pin. Shift clock for the vertical driver
     * @param   enb: ENB  / GEN pin. Write enable signal for the pixel memory
     * @param   xrst: XRST / INTB pin. Reset signal for the horizontal and vertical driver
     * @param   hst: HST  / BSP pin. Start signal for the horizontal driver
     * @param   hck: HCK  / BCK pin. Shift clock for the horizontal driver
     * @param   frp: FRP  / VB pin. Liquid crystal driving signal ("Off" pixel)
     * @param   xfrp: XFRP / VA pin. Liquid crystal driving signal ("On" pixel)
     * @param   r1: R1 pin. Red image data (odd pixels)
     * @param   r2: R2 pin. Red image data (even pixels)
     * @param   g1: G1 pin. Green image data (odd pixels)
     * @param   g2: G2 pin. Green image data (even pixels)
     * @param   b1: B1 pin. Blue image data (odd pixels)
     * @param   b2: B2 pin. Blue image data (even pixels)
     */
    void config(Interface::IGpo *vst, Interface::IGpo *vck, Interface::IGpo *enb,
                Interface::IGpo *xrst, Interface::IGpo *hst, Interface::IGpo *hck,
                Interface::IGpo *frp, Interface::IGpo *xfrp,
                Interface::IGpo *r1, Interface::IGpo *r2,
                Interface::IGpo *g1, Interface::IGpo *g2,
                Interface::IGpo *b1, Interface::IGpo *b2);

    /**
     * @brief   Initialize LPM012M387D object.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() override;

    /**
     * @brief   Deinitialize LPM012M387D object and free all resources.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() override;

    /**
     * @brief   Turn on LPM012M387D.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool enable() override;

    /**
     * @brief   Turn off LPM012M387D or put it to low power mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool disable() override;

    /**
     * @brief   Check if the LPM012M387D is on.
     * @retval  'true' - LPM012M387D is on, 'false' - otherwise.
     */
    virtual bool isEnabled() override;

    /**
     * @brief   Configure LPM012M387D resolution.
     * @param   width: LPM012M387D width.
     * @param   height: LPM012M387D height.
     */
    virtual void setResolution(int16_t width, int16_t height) override
    {
        // This driver has fixed resolution
        (void) width;
        (void) height;
    }

    /**
     * @brief   Get LPM012M387D resolution.
     * @param   width: LPM012M387D width.
     * @param   height: LPM012M387D height.
     */
    virtual void getResolution(int16_t &width, int16_t &height) override
    {
        width  = skWidth;
        height = skHeight;
    }

    virtual uint8_t getDisplayColorDepth() override
    {
        return 6;
    }

    /**
     * @brief   Write data to LCD.
     * @param   pBuf: Frame buffer.
     */
    virtual void write(const uint8_t *pBuf) override;

protected:

private:

    static const int16_t skWidth = 240;
    static const int16_t skHeight = 240;

    // Component init flag
    bool mIsInit { };

    // LCD is on
    bool mIsOn { };

    // Timers for driving Liquid crystal
    LPTIM_HandleTypeDef mLptimFRP { };
    LPTIM_HandleTypeDef mLptimXFRP { };

    void GPIO_Init();
    void GPIO_DeInit();
    uint16_t GetPeriod();
    void LPTIM_FRP_Init();
    void LPTIM_XFRP_Init();
    void LPTIM_FRP_DeInit();
    void LPTIM_XFRP_DeInit();

    /**
     * @brief   An empty callback to be sure that the HAL implementation will
     *          not be called.
     */
    static void MspEmptyCallback(LPTIM_HandleTypeDef *lptimHandle)
    {
    }

};

} /* namespace Driver */

#endif /* __HARDWARE_LS012B7DD06A_HPP */
