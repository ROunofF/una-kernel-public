/**
 ******************************************************************************
 * @file    ProdTestFactory.hpp
 * @brief   Factory for creating production tests by its name.
 *
 * @details Maps a CLI-friendly name (e.g. "PMIC") to a canonical test ID
 *          (e.g. "ID_PMIC") and constructs a corresponding Interface::IProdTest.
 *          The canonical ID is forwarded to the test's constructor so that test
 *          code does not hardcode the name, avoiding duplication.
 ******************************************************************************
 */

#ifndef __PROD_TEST_FACTORY_HPP
#define __PROD_TEST_FACTORY_HPP

#include "ProdTest/IProdTest.hpp"
#include "ProdTest/ProdTestPMIC.hpp"
#include "ProdTest/ProdTestButtons.hpp"
#include "ProdTest/ProdTestButtonsCheck.hpp"
#include "ProdTest/ProdTestFuelGauge.hpp"
#include "ProdTest/ProdTestFlash.hpp"
#include "ProdTest/ProdTestFileSystem.hpp"
#include "ProdTest/ProdTestVibro.hpp"
#include "ProdTest/ProdTestBuzzer.hpp"
#include "ProdTest/ProdTestLCD.hpp"
#include "ProdTest/ProdTestLcdEeprom.hpp"
#include "ProdTest/ProdTestLcdBackLight.hpp"
#include "ProdTest/ProdTestBLE.hpp"
#include "ProdTest/ProdTestBleName.hpp"
#include "ProdTest/ProdTestBleEeprom.hpp"
#include "ProdTest/ProdTestAltimeter.hpp"
#include "ProdTest/ProdTestBodySensor.hpp"
#include "ProdTest/ProdTestBodySensorFactoryTest.hpp"
#include "ProdTest/ProdTestBodySensorEeprom.hpp"
#include "ProdTest/ProdTestIMU.hpp"
#include "ProdTest/ProdTestImuEeprom.hpp"
#include "ProdTest/ProdTestMagnetometer.hpp"
#include "ProdTest/ProdTestMagnetometerEeprom.hpp"
#include "ProdTest/ProdTestGPS.hpp"
#include "ProdTest/ProdTestGpsEeprom.hpp"
#include "ProdTest/ProdTestGpsCommunication.hpp"
#include "ProdTest/ProdTestGpsProgrammingStatus.hpp"
#include "ProdTest/ProdTestVersion.hpp"
#include "ProdTest/ProdTestUSB.hpp"

#include <string>
#include <memory>

class ProdTestFactory final {
public:
    static std::unique_ptr<Interface::IProdTest> createTest(std::string name);

private:
    static std::unique_ptr<Interface::IProdTest> makePMIC(std::string name)
    {
        return std::make_unique<ProdTest::PMIC>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeButtons(std::string name)
    {
        return std::make_unique<ProdTest::Buttons>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeButtonsCheck(std::string name)
    {
        return std::make_unique<ProdTest::ButtonsCheck>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeFuelGauge(std::string name)
    {
        return std::make_unique<ProdTest::FuelGauge>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeFlash(std::string name)
    {
        return std::make_unique<ProdTest::Flash>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeFileSystem(std::string name)
    {
        return std::make_unique<ProdTest::FileSystem>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeVibro(std::string name)
    {
        return std::make_unique<ProdTest::Vibro>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeBuzzer(std::string name)
    {
        return std::make_unique<ProdTest::Buzzer>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeLcd(std::string name)
    {
        return std::make_unique<ProdTest::Lcd>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeLcdEeprom(std::string name)
    {
        return std::make_unique<ProdTest::LcdEeprom>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeLcdBacklight(std::string name)
    {
        return std::make_unique<ProdTest::LcdBackLight>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeBle(std::string name)
    {
        return std::make_unique<ProdTest::Ble>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeBleName(std::string name)
    {
        return std::make_unique<ProdTest::BleName>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeBleEeprom(std::string name)
    {
        return std::make_unique<ProdTest::BleEeprom>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeAltimeter(std::string name)
    {
        return std::make_unique<ProdTest::Altimeter>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeBodySensor(std::string name)
    {
        return std::make_unique<ProdTest::BodySensor>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeBSFactoryTester(std::string name)
    {
        return std::make_unique<ProdTest::BodySensorFactoryTest>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeBodySensorEeprom(std::string name)
    {
        return std::make_unique<ProdTest::BodySensorEeprom>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeImu(std::string name)
    {
        return std::make_unique<ProdTest::Imu>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeImuEeprom(std::string name)
    {
        return std::make_unique<ProdTest::ImuEeprom>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeMagnetometer(std::string name)
    {
        return std::make_unique<ProdTest::Magnetometer>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeMagnetometerEeprom(std::string name)
    {
        return std::make_unique<ProdTest::MagnetometerEeprom>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeGps(std::string name)
    {
        return std::make_unique<ProdTest::Gps>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeGpsEeprom(std::string name)
    {
        return std::make_unique<ProdTest::GpsEeprom>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeGpsCommunication(std::string name)
    {
        return std::make_unique<ProdTest::GpsCommunication>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeGpsProgrammingStatus(std::string name)
    {
        return std::make_unique<ProdTest::GpsProgrammingStatus>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeVersion(std::string name)
    {
        return std::make_unique<ProdTest::Version>(name);
    }

    static std::unique_ptr<Interface::IProdTest> makeUsb(std::string name)
    {
        return std::make_unique<ProdTest::USB>(name);
    }
};

#endif
