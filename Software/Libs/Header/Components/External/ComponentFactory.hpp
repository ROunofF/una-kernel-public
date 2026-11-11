/**
 ******************************************************************************
 * @file    ComponentFactory.hpp
 * @brief   Factory to create components that present in the system.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_COMPONENT_FACTORY_HPP
#define COMPONENT_COMPONENT_FACTORY_HPP

#include "ILcd.hpp"
#include "IBacklight.hpp"
#include "IBle.hpp"
#include "IGps.hpp"
#include "IImu.hpp"
#include "IMagnetometer.hpp"
#include "IBarometer.hpp"

/**
 * @brief   Factory to create components that present in the system.
 */
class ComponentFactory
{

public:

    /**
     * @brief   Get ComponentFactory instance.
     * @retval  ComponentFactory instance.
     */
    static ComponentFactory& GetInstance()
    {
        static ComponentFactory sInstance;
        return sInstance;
    }

    void scanModules();

    Interface::ILcd& getLcd();
    bool isLcdPresent()
    {
        return mpLcd != nullptr;
    }

    Interface::IBacklight& getBacklight();
    bool isBacklightPresent()
    {
        return mpBacklight != nullptr;
    }

    Interface::IBle& getBle();
    bool isBlePresent()
    {
        return mpBle != nullptr;
    }

    Interface::IGps& getGps();
    bool isGpsPresent()
    {
        return mpGps != nullptr;
    }

    Interface::IMagnetometer& getMagnetometer();
    bool isMagnetometerPresent()
    {
        return mpMagnetometer != nullptr;
    }

    /// Whether the part is there is the answer of its init(), not of this.
    Interface::IBarometer& getBarometer();

    Interface::IImu& getImu();
    bool isImuPresent()
    {
        return mpImu != nullptr;
    }

protected:

    /**
     * @brief   Constructor.
     */
    ComponentFactory() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~ComponentFactory() = default;

private:

    Interface::ILcd *mpLcd = nullptr;
    Interface::IBacklight *mpBacklight = nullptr;
    Interface::IBle *mpBle = nullptr;
    Interface::IGps *mpGps = nullptr;
    Interface::IMagnetometer *mpMagnetometer = nullptr;
    Interface::IBarometer *mpBarometer = nullptr;
    Interface::IImu *mpImu = nullptr;

};

#endif // COMPONENT_COMPONENT_FACTORY_HPP
