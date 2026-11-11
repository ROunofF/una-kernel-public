/**
 ******************************************************************************
 * @file    BatteryService.hpp
 * @brief   BLE Battery Service (BAS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_SERVICE_BAS_HPP
#define __COMPONENT_BLE_SERVICE_BAS_HPP

#include "IBle.hpp"

namespace Ble
{

/**
 * @brief   BLE Battery Service (BAS).
 */
class BatteryService : virtual public Interface::IBle::StatusCallback,
        virtual public Interface::IBle::CharacteristicCallback {

public:

    class Callback {
    public:
        virtual void onReadBatteryLevel(float &level) {}
    protected:
        virtual ~Callback() = default;
    };

    /**
     * @brief   Constructor.
     * @param   ble: BLE instance.
     * @param   pCallback: Operation callback.
     */
    BatteryService(Interface::IBle &ble, Callback *pCallback);

    /**
     * @brief   Destructor.
     */
    virtual ~BatteryService();

    /**
     * @brief   Must be called when the battery level changes by 1%.
     * @param   level: Current battery level as a percentage (0.0 - 100.0).
     */
    void onBatteryLevelChanged(float level);

private:

    Interface::IBle &mBle;
    Callback *mObserver = nullptr;

    ServiceHandle mSrv { };
    CharacteristicHandle mChrBatteryLevel { };

    bool mNotificationEnabled = false;

    // The battery level that was last sent to the client
    uint8_t mBatteryLevel = 0;


    /**
     * @copydoc Interface::IBle::StatusCallback::onDisconnect()
     */
    virtual void onDisconnect(uint8_t reason) override;

    /**
     * @brief   Called when a characteristic's notification or indication
     *          is enabled/disabled.
     * @param   chrHandle: Handle of the characteristic.
     * @param   notify: 'true' if notifications are enabled, 'false' disabled.
     * @param   indicate:'true' if indications are enabled, 'false' disabled.
     */
    virtual void onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
            bool notify, bool indicate) override;

    /**
     * @brief   Called when the remote device reads a characteristic's value.
     * @param   chrHandle - Handle of the characteristic being read.
     */
    virtual void onRead(Ble::CharacteristicHandle chrHandle) override;

};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_SERVICE_BAS_HPP */
