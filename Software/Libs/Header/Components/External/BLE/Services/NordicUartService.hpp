/**
 ******************************************************************************
 * @file    NordicUartService.hpp
 * @brief   Nordic UART Service (NUS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_SERVICE_NUS_HPP
#define __COMPONENT_BLE_SERVICE_NUS_HPP

#include "IBle.hpp"
#include <string_view>

namespace Ble
{

/**
 * @brief   Nordic UART Service (NUS).
 */
class NordicUartService : virtual public Interface::IBle::StatusCallback,
        virtual public Interface::IBle::CharacteristicCallback {

public:

    class Callback {
    public:
        virtual void onRx(std::string_view str) {}
    protected:
        virtual ~Callback() = default;
    };

    /**
     * @brief   Constructor.
     * @param   ble: BLE instance.
     * @param   pCallback: Operation callback.
     */
    NordicUartService(Interface::IBle &ble, Callback *pCallback);

    /**
     * @brief   Destructor.
     */
    virtual ~NordicUartService();

    /**
     * @brief   Send data.
     * @param   str: data to send.
     */
    void onTx(std::string_view str);

private:

    Interface::IBle &mBle;
    Callback *mObserver = nullptr;

    ServiceHandle mSrv {};
    CharacteristicHandle mChrTx {};
    CharacteristicHandle mChrRx {};

    bool mNotificationEnabled = false;


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
     * @brief   Called when data is written to a characteristic by the
     *          remote device.
     * @param   chrHandle: Handle of the characteristic being written to.
     * @param   pData: Pointer to the data that was written.
     * @param   len: Length of the data in bytes.
     */
    virtual void onWrite(Ble::CharacteristicHandle chrHandle,
            const uint8_t *pData, uint16_t len) override;

};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_SERVICE_NUS_HPP */
