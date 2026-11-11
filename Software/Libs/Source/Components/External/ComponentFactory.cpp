/**
 ******************************************************************************
 * @file    ComponentFactory.cpp
 * @brief   Factory to create components that present in the system.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ComponentFactory"
#define LOG_MODULE_LEVEL    LOG_LEVEL_WARNING
#include "Logger.h"

#include "ComponentFactory.hpp"

#include "Instances/InstanceI2C.hpp"
#include "Instances/InstanceUART.hpp"
#include "Instances/InstanceSPI.hpp"

#include "I2c.hpp"
#include "Gpio.hpp"
#include "Pwm.hpp"
#include "Spi.hpp"

#include "N24S64B.hpp"

#include "Backlight.hpp"
#include "Stub/BacklightStub.hpp"

#include "LS012B7DD06A.hpp"
#include "Stub/LcdStub.hpp"

#include "CoreBlueNRG.hpp"
#include "PeripheralBlueNRG.hpp"
#include "Accessory/AccessoryCentral.hpp"
#include "Accessory/ProfileHrs.hpp"
#include "Stub/BleServerStub.hpp"

#include "GpsAiroha.hpp"
#include "Stub/GpsReceiverStub.hpp"

#include "Magnetometer/MagBosch.hpp"
#include "Stub/MagnetometerStub.hpp"

#include "Barometer/BaroMs5837.hpp"
#include "Stub/BarometerStub.hpp"

#include "IMU/ImuBosch.hpp"
#include "Stub/ImuStub.hpp"


void ComponentFactory::scanModules()
{
    Interface::II2c &i2cMain = Instance::I2C::getInstance().getMainBus();
    i2cMain.init();

    Interface::II2c &i2cBody = Instance::I2C::getInstance().getBodyBus();
    i2cBody.init();

    Interface::II2c &i2cSens1 = Driver::I2c::GetInstance(Driver::I2c::Port::PORT_3);
    i2cSens1.init();

    Interface::II2c &i2cSens2 = Driver::I2c::GetInstance(Driver::I2c::Port::PORT_4);
    i2cSens2.init();

    // LCD & Backlight
    {
        Hardware::N24S64B eeprom {i2cMain};
        Hardware::N24S64B::UniqueId_t uniqueId {};
        if (eeprom.isReady() && eeprom.readUniqueId(uniqueId)) {
            LOG_INFO("LCD bus. ID:\n");
            LOG_INFO_DUMP_WP(uniqueId.bytes, Hardware::N24S64B::skUniqueIdSize);

            if (mpLcd == nullptr) {
                Hardware::LS012B7DD06A *lcd =
                        new (std::nothrow) Hardware::LS012B7DD06A();
                if (lcd) {
                    /*
                     lcd->config(vst,
                     vck,
                     enb,
                     xrst,
                     hst,
                     hck,
                     frp,
                     xfrp,
                     r1,
                     r2,
                     g1,
                     g2,
                     b1,
                     b2);
                     */
                    mpLcd = lcd;
                }
            }

            if (mpBacklight == nullptr) {
                Backlight *bl = new (std::nothrow) Backlight(
                        Driver::Gpio::GetInstance( Driver::Gpio::Pin::BACKLIGHT_ON));
                if (bl) {
                    mpBacklight = bl;
                }
            }
        }

        if (mpLcd == nullptr) {
            LOG_ERROR("LCD not found\n");
        }

        if (mpBacklight == nullptr) {
            LOG_ERROR("Backlight not found\n");
        }
    }

    // Body bus
    {
        // The barometer carries no identity of its own to look for here,
        // so the component is always created: its init() powers the part
        // and reads its calibration, and that is what answers whether it
        // is there.
        if (mpBarometer == nullptr) {
            BaroMs5837 *pBaro = new (std::nothrow) BaroMs5837();

            // Not deleted when config() refuses it: a refusal is a build
            // fault, and the destructor would wait on the part's worker
            // during boot.
            if (pBaro != nullptr &&
                pBaro->config(i2cBody,
                              Driver::Gpio::GetInstance(
                                      Driver::Gpio::Pin::ALTIMETER_EN))) {
                mpBarometer = pBaro;
            }
        }

        if (mpBarometer == nullptr) {
            LOG_ERROR("Barometer could not be created\n");
        }
    }

    // Sensor 1 bus
    {
        Hardware::N24S64B eeprom {i2cSens1};
        Hardware::N24S64B::UniqueId_t uniqueId {};
        if (eeprom.isReady() && eeprom.readUniqueId(uniqueId)) {
            LOG_INFO("Sensor 1 bus. ID:\n");
            LOG_INFO_DUMP_WP(uniqueId.bytes, Hardware::N24S64B::skUniqueIdSize);

#if 1
            if (mpBle == nullptr) {
                Ble::CoreBlueNRG *pCore = new (std::nothrow) Ble::CoreBlueNRG();
                if (pCore) {
                    pCore->config(
                            &Instance::SPI::GetInstance(
                                    Driver::Spi::Port::PORT_1),
                            &Driver::Gpio::GetInstance(
                                    Driver::Gpio::Pin::SENSOR1_CS),
                            &Driver::Gpio::GetInstance(
                                    Driver::Gpio::Pin::SENSOR1_TX_INT0),
                            &Driver::Gpio::GetInstance(
                                    Driver::Gpio::Pin::SENSOR1_RST_L));

                    Ble::PeripheralBlueNRG *pPeripheral =
                            new (std::nothrow) Ble::PeripheralBlueNRG(*pCore);
                    mpBle = pPeripheral;

                    // Central-role accessory link (external HR strap). Dormant
                    // until an app requests acquisition; self-registers
                    // with the BLE event router. v1 ships the HRS profile only.
                    if (pPeripheral) {
                        Ble::ProfileHrs *pProfile =
                                new (std::nothrow) Ble::ProfileHrs();
                        if (pProfile) {
                            new (std::nothrow) Ble::AccessoryCentral(
                                    *pCore, *pProfile, *pPeripheral);
                        }
                    }
                }
            }
#endif
        }

        if (mpBle == nullptr) {
            LOG_ERROR("BLE not found\n");
        }
    }

    // Sensor 2 bus
    {
        Hardware::N24S64B eeprom {i2cSens2};
        Hardware::N24S64B::UniqueId_t uniqueId {};
        if (eeprom.isReady() && eeprom.readUniqueId(uniqueId)) {
            LOG_INFO("Sensor 2 bus. ID:\n");
            LOG_INFO_DUMP_WP(uniqueId.bytes, Hardware::N24S64B::skUniqueIdSize);

            if (mpGps == nullptr) {
                Interface::IUart &uart = Instance::UART::getInstance().getSensor2Bus();
                Interface::IGpo &pwr = Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_RST_L);

                Gps::GpsAiroha *pGps = new (std::nothrow) Gps::GpsAiroha(uart, pwr);

                if (pGps) {
                    mpGps = pGps;
                }
            }

            if (mpMagnetometer == nullptr) {
                // The EEPROM check above answers for this part too. It goes
                // through the wrapped bus handle, which serialises the port.
                // MAG_INT is on PC2, named SENSOR2_MISO after its SPI role.
                MagBosch *pMag = new (std::nothrow) MagBosch();

                // Not deleted when config() refuses it: a refusal is a build
                // fault, and the destructor would wait on the part's worker
                // during boot.
                if (pMag != nullptr &&
                    pMag->config(Instance::I2C::getInstance().getSensor2Bus(),
                                 Driver::Gpio::GetInstance(
                                         Driver::Gpio::Pin::SENSOR2_MISO))) {
                    mpMagnetometer = pMag;
                }
            }

            if (mpImu == nullptr) {
                // On the same board, so the EEPROM check above answers for
                // it as well.
                ImuBosch *pImu = new (std::nothrow) ImuBosch();

                if (pImu != nullptr &&
                    pImu->config(Instance::I2C::getInstance().getSensor2Bus(),
                                 Driver::Gpio::GetInstance(
                                         Driver::Gpio::Pin::SENSOR2_INT2))) {
                    mpImu = pImu;
                } else {
                    LOG_ERROR("IMU could not be created\n");
                }
            }
        }

        if (mpGps == nullptr) {
            LOG_ERROR("GPS not found\n");
        }

        if (mpMagnetometer == nullptr) {
            LOG_ERROR("Magnetometer not found\n");
        }

        if (mpImu == nullptr) {
            LOG_ERROR("IMU not found\n");
        }
    }

}

Interface::IBacklight& ComponentFactory::getBacklight()
{
    static Stub::Backlight stub;
    if (mpBacklight) {
        return *mpBacklight;
    }
    return stub;
}

Interface::ILcd& ComponentFactory::getLcd()
{
    static Stub::Lcd stub;
    if (mpLcd) {
        return *mpLcd;
    }
    return stub;
}

Interface::IBle& ComponentFactory::getBle()
{
    static Stub::BleServer stub;
    if (mpBle) {
        return *mpBle;
    }
    return stub;
}

Interface::IGps& ComponentFactory::getGps()
{
    static Stub::GpsReceiverStub stub;
    if (mpGps) {
        return *mpGps;
    }
    return stub;
}

Interface::IMagnetometer& ComponentFactory::getMagnetometer()
{
    static Stub::Magnetometer stub;
    if (mpMagnetometer) {
        return *mpMagnetometer;
    }
    return stub;
}

Interface::IBarometer& ComponentFactory::getBarometer()
{
    static Stub::Barometer stub;
    if (mpBarometer) {
        return *mpBarometer;
    }
    return stub;
}

Interface::IImu& ComponentFactory::getImu()
{
    static Stub::Imu stub;
    if (mpImu) {
        return *mpImu;
    }
    return stub;
}

