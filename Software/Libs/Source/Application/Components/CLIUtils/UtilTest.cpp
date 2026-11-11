/**
 *******************************************************************************
 * @file    UtilTest.cpp
 * @brief   CLI utility to run production tests and print results/details.
 *
 * @details Usage: TEST <ID_TEST>
 *          Examples:
 *            - TEST ID_PMIC
 *
 *          The utility asks ProdTestFactory to create the test by ID,
 *          runs it via ProdTestController, and mirrors details to console
 *          and log.
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Components/CLIUtils/UtilTest.hpp"
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"

#include "ProdTest/Core/ProdTestController.hpp"
#include "ProdTest/Core/ProdTestFactory.hpp"

#include <string>

namespace Components::CliUtils {

/**
 * @brief Construct the TEST CLI utility (command name is "TEST").
 */
UtilTest::UtilTest() :
    UtilBase("TEST"),
    mCommands{
        .execute = UtilTest::execute,
        .getName = UtilTest::getName,
        .getDesc = UtilTest::getDesc,
        .getHelp = UtilTest::getHelp,
    }
{}

/**
 * @brief Get singleton instance of the TEST CLI utility.
 * @return Reference to the utility object.
 */
Components::CliUtils::UtilTest& UtilTest::getInstance()
{
    static UtilTest instance;

    return instance;
}

/**
 * @brief Get the table with pointers to command handlers.
 * @return Pointer to ICLICommand_t.
 */
ICLICommand_t* UtilTest::getCommands()
{
    return &mCommands;
}

/**
 * @brief Get the utility name ("TEST").
 * @param cmd Unused.
 * @return Utility name string.
 */
const char* UtilTest::getName(ICLICommand_t* cmd)
{
    (void) cmd;

    return UtilTest::getInstance().getUtilName();
}

/**
 * @brief Get a short description of the utility.
 * @param cmd Unused.
 * @return Brief description string.
 */
const char* UtilTest::getDesc(ICLICommand_t* cmd)
{
    (void) cmd;

    return "run production tests by ID";
}

/**
 * @brief Print help/usage for the utility.
 * @param cmd Unused.
 * @return Empty string (API contract).
 */
const char* UtilTest::getHelp(ICLICommand_t* cmd)
{
    (void) cmd;

    static const char* help =
        "The list of the internal commands:\n"
        "\n"
        CLI_ATTR_CMD_HELP "TEST <ID_TEST_NAME>" CLI_ATTR_DEF "\n"
        "   runs a production test by its ID\n"
        "\n"
        " Available ID_TEST values:\n"
        "   - " CLI_ATTR_CMD_HELP "ID_PMIC                       " CLI_ATTR_DEF " : runs the PMIC\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BUTTONS                    " CLI_ATTR_DEF " : runs the buttons\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BUTTONS_CHECK              " CLI_ATTR_DEF " : runs the buttons (extended)\n"
        "       Lights up corresponding LCD sectors when buttons are pressed\n"
        "       Internal commands:\n"
        "         SW_CHECK - returns the current state of the buttons\n"
        "         exit     - stops this test\n"
        "\n"
        "       The test will also stop if no button is pressed or no command is\n"
        "       received within 10 seconds\n"
        "   - " CLI_ATTR_CMD_HELP "ID_FUELGAUGE                  " CLI_ATTR_DEF " : runs the fuel gauge\n"
        "   - " CLI_ATTR_CMD_HELP "ID_FLASH                      " CLI_ATTR_DEF " : runs the flash\n"
        "   - " CLI_ATTR_CMD_HELP "ID_FILESYSTEM                 " CLI_ATTR_DEF " : runs the flash and file system\n"
        "   - " CLI_ATTR_CMD_HELP "ID_VIBRO                      " CLI_ATTR_DEF " : runs the vibro\n"
        "     Internal commands:\n"
        "       vibro\n"
        "           Starts the vibro for 10s\n"
        "\n"
        "       verify [options]\n"
        "           Starts the vibro for 1s and verifies it using the IMU\n"
        "\n"
        "           Options:\n"
        "             -t <imu_threshold>    IMU threshold (default: 1.5)\n"
        "             -d <duration_ms>      Test duration in ms (default: 600)\n"
        "             --no-vibro            Do not start the vibro\n"
        "             --details             Show verification details\n"
        "             --xlsamples           Show IMU samples\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BUZZER                     " CLI_ATTR_DEF " : runs the buzzer\n"
        "     Internal commands:\n"
        "     level v - starts the buzzer with level v for 1s. Variants - 1, 2, 3\n"
        "     exit    - stops this test\n"
        "   - " CLI_ATTR_CMD_HELP "ID_LCD                        " CLI_ATTR_DEF " : runs the LCD\n"
        "     Internal commands:\n"
        "     color v - color name. Variants - RED, GREEN, BLUE, WHITE\n"
        "     exit    - stops this test\n"
        "\n"
        "   - " CLI_ATTR_CMD_HELP "ID_LCD_EEPROM                 " CLI_ATTR_DEF " : runs the LCD EEPROM\n"
        "   - " CLI_ATTR_CMD_HELP "ID_LCD_BACKLIGHT              " CLI_ATTR_DEF " : runs the LCD BACKLIGHT\n"
        "     Internal commands:\n"
        "     ON   - turns on the backlight\n"
        "     OFF  - turns off the backlight\n"
        "     exit - stops this test\n"
        "\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BLE                        " CLI_ATTR_DEF " : runs the BLE\n"
        "     Options:\n"
        "       --unpair              Remove all stored bonds instead of pairing\n"
        "\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BLE_NAME                   " CLI_ATTR_DEF " : runs the BLE NAME\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BLE_EEPROM                 " CLI_ATTR_DEF " : runs the BLE EEPROM\n"
        "   - " CLI_ATTR_CMD_HELP "ID_ALTIMETER                  " CLI_ATTR_DEF " : runs the altimeter\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BODYSENSOR                 " CLI_ATTR_DEF " : runs the body sensor\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BS_FACTORY_TESTER          " CLI_ATTR_DEF " : runs the body sensor factory tests\n"
        "   - " CLI_ATTR_CMD_HELP "ID_BODYSENSOR_EEPROM          " CLI_ATTR_DEF " : runs the body sensor EEPROM\n"
        "   - " CLI_ATTR_CMD_HELP "ID_IMU                        " CLI_ATTR_DEF " : runs the IMU\n"
        "   - " CLI_ATTR_CMD_HELP "ID_IMU_EEPROM                 " CLI_ATTR_DEF " : runs the IMU EEPROM\n"
        "   - " CLI_ATTR_CMD_HELP "ID_MAGNETOMETER               " CLI_ATTR_DEF " : runs the magnetometer\n"
        "     Options:\n"
        "       --int                 Also check the data-ready line (PC2)\n"
        "\n"
        "   - " CLI_ATTR_CMD_HELP "ID_MAGNETOMETER_EEPROM        " CLI_ATTR_DEF " : runs the magnetometer EEPROM\n"
        "   - " CLI_ATTR_CMD_HELP "ID_GPS                        " CLI_ATTR_DEF " : runs the GPS\n"
        "   - " CLI_ATTR_CMD_HELP "ID_GPS_EEPROM                 " CLI_ATTR_DEF " : runs the GPS EEPROM\n"
        "   - " CLI_ATTR_CMD_HELP "ID_GPS_COMMUNICATION          " CLI_ATTR_DEF " : runs the GPS communication\n"
        "\n"
        "   - " CLI_ATTR_CMD_HELP "ID_GPS_PROGRAMMING_STATUS     " CLI_ATTR_DEF " : runs the GSP programming status\n"
        "   - " CLI_ATTR_CMD_HELP "ID_USB                        " CLI_ATTR_DEF " : runs the USB protection test\n"
        "     Internal commands:\n"
        "     exit - exits from this test\n"
        "\n"
        "   - " CLI_ATTR_CMD_HELP "ID_FIRMWARE_VERSION           " CLI_ATTR_DEF " : outputs bootloader, LibC, and kernel versions\n"
        "\n"
        "Examples\n"
        "\n"
        CLI_ATTR_CMD_HELP "TEST ID_PMIC" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "TEST ID_BUZZER -dur 2000 -level 1" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "TEST ID_BLE --unpair" CLI_ATTR_DEF "\n"
        "\n"
        CLI_ATTR_CMD_HELP "TEST ID_VIBRO" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> vibro" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> verify -t 1.0" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> verify -t 2.5 -d 300" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> verify -t 2.5 -d 300 --no-vibro --details" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> verify -t 0.2 --details --xlsamples" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> exit" CLI_ATTR_DEF "\n"
        "\n"
        CLI_ATTR_CMD_HELP "TEST ID_BUZZER" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> level 1" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> level 2" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> level 3" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> exit" CLI_ATTR_DEF "\n"
        "\n"
        CLI_ATTR_CMD_HELP "TEST ID_LCD" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> color GREEN" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> color RED" CLI_ATTR_DEF "\n"
        CLI_ATTR_CMD_HELP "> exit" CLI_ATTR_DEF "\n"
        "\n";

    return help;
}

/**
 * @brief Execute the TEST utility: parse args, run test, print results.
 *
 * @param cmd   Unused.
 * @param input Unused.
 * @param argc  Argument count; must be 2 (\"TEST\" and <TEST_ID>).
 * @param argv  Argument values; argv[1] is the <TEST_ID>.
 */
void UtilTest::execute(ICLICommand_t* cmd,
                       CLIInput_t*    input,
                       int            argc,
                       const char**   argv)
{
    (void) cmd;

    if (argc < 2) {
        CLIConsole_print(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }

    ++argv;
    --argc;

    std::string name(*argv);

    auto test = ProdTestFactory::createTest(name);
    if (!test) {
        CLIConsole_print("unknown test ID: %s\n", name.c_str());
        return;
    }

    CLIParser_t p;
    CLIParser_init(&p, argc, argv);

    ProdTestController controller(*test.get(), p, input);
    controller.execute();
}

} // namespace Components::CliUtils
