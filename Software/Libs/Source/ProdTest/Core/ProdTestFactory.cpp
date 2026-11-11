/**
 ******************************************************************************
 * @file    ProdTestFactory.cpp
 * @brief   Implementation of production test factory.
 ******************************************************************************
 */

#include "ProdTest/Core/ProdTestFactory.hpp"

std::unique_ptr<Interface::IProdTest> ProdTestFactory::createTest(std::string name)
{
    using Factory = std::unique_ptr<Interface::IProdTest>(*)(std::string);

    static std::pair<const char*, Factory> kRegistry[] = {
        {"ID_PMIC",                   &makePMIC},
        {"ID_BUTTONS",                &makeButtons},
        {"ID_BUTTONS_CHECK",          &makeButtonsCheck},
        {"ID_FUELGAUGE",              &makeFuelGauge},
        {"ID_FLASH",                  &makeFlash},
        {"ID_FILESYSTEM",             &makeFileSystem},
        {"ID_VIBRO",                  &makeVibro},
        {"ID_BUZZER",                 &makeBuzzer},
        {"ID_LCD",                    &makeLcd},
        {"ID_LCD_EEPROM",             &makeLcdEeprom},
        {"ID_LCD_BACKLIGHT",          &makeLcdBacklight},
        {"ID_BLE",                    &makeBle},
        {"ID_BLE_NAME",               &makeBleName},
        {"ID_BLE_EEPROM",             &makeBleEeprom},
        {"ID_ALTIMETER",              &makeAltimeter},
        {"ID_BODYSENSOR",             &makeBodySensor},
        {"ID_BS_FACTORY_TESTER",      &makeBSFactoryTester},
        {"ID_BODYSENSOR_EEPROM",      &makeBodySensorEeprom},
        {"ID_IMU",                    &makeImu},
        {"ID_IMU_EEPROM",             &makeImuEeprom},
        {"ID_MAGNETOMETER",           &makeMagnetometer},
        {"ID_MAGNETOMETER_EEPROM",    &makeMagnetometerEeprom},
        {"ID_GPS",                    &makeGps},
        {"ID_GPS_EEPROM",             &makeGpsEeprom},
        {"ID_GPS_COMMUNICATION",      &makeGpsCommunication},
        {"ID_GPS_PROGRAMMING_STATUS", &makeGpsProgrammingStatus},
        {"ID_FIRMWARE_VERSION",       &makeVersion},
        {"ID_USB",                    &makeUsb},
    };

    for (const auto& [key, ctor] : kRegistry) {
        if (name == key) {
            return ctor(name);
        }
    }

    return {}; // not found
}
