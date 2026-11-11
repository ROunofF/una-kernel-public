/**
 ******************************************************************************
 * @file    main.сpp
 * @brief   Main program body.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "main.h"

#define LOG_MODULE_PRX      "main"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include <cstring>

#include "Porting.h"
#include "PriorityMap.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

#include "LogUart.hpp"
#include "System.hpp"
#include "Wdg.hpp"
#include "Rtc.hpp"
#if (USE_SYSTEM_SLEEP == 1)
#include "Sleep.hpp"
#endif // (USE_SYSTEM_SLEEP == 1)

#include "HeapStackMonitor.hpp"
#include "ErrorMonitor.hpp"
#include "Observer.hpp"

#include "Gpio.hpp"
#include "I2c.hpp"
#include "Pwm.hpp"
#include "FuelGauge.hpp"
#include "Pmic.hpp"
#include "Flash.hpp"
#include "flash_diskio.h"
#include "FileSystem.hpp"
#include "UsbDevice.hpp"
#include "Power.hpp"
#include "HWButtons.hpp"
#include "Buzzer.hpp"
#include "Vibro.hpp"

#include "ComponentFactory.hpp"
#include "AppSurface.hpp"

#include "SettingsManager.hpp"
#include "SettingsLocalManager.hpp"
#include "SettingsDebugManager.hpp"
#include "SettingsAppSorting.hpp"

#include "app_touchgfx.h"

#include "Backend.hpp"

#include "Instances/InstanceI2C.hpp"
#include <Instances/InstanceCLI.hpp>
#include "Instances/InstanceSensorLayer.hpp"
#include "Instances/InstanceTimerSysMs.h"
#include "Instances/InstanceUsbProtection.hpp"

#include "SwTimer.hpp"
#include "Time/TimePorting.hpp"

#include "Crash/CrashDump.h"
#include "crash_glue.h"
#include <ctime>
#include "LogBuffer/LogBuffer.hpp"
#include "Loggers/SystemLogger.hpp"
#include "BleLogger.hpp"

#include "Test/Test.hpp"

#define WDG_ON          (1)     // Enable watchdog
#define VIBRO_ON        (1)     // Enable vibration motor at startup
#define WORKMODE_ON     (1)     // Enable normal mode; 0 selects test mode
#define SLEEP_ON        (1)     // Enable sleep mode

// MSP (ISR) stack watermark. Defined in heap_useNewlib_ST.cpp, which
// owns the linker symbols that bound the region.
extern "C" void Heap_paintIsrStack(void);
extern "C" void Heap_armIsrStackLimit(void);

// Private function prototypes
static void StartupTask(void *param);
static void SystemPower_Config(void);
static void PinsDefaultConfig(void);

/**
 * @brief Central log sink registered with the logger (single fan-out point).
 *
 * Order matters: the no-init LogBuffer is fed first so the log is captured even
 * before UART is up and regardless of later failures, then mirrored to UART and
 * (when enabled) BLE.
 */
static void Kernel_logSink(const char *data, size_t len)
{
    LogBuffer::write(reinterpret_cast<const uint8_t*>(data),
                     static_cast<uint16_t>(len));

    // UART mirror. Driver::LogUart::Write() ends in HAL_UART_Transmit(), whose
    // TXE/TC waits are bounded by a HAL_GetTick() timeout - but HAL_GetTick() is
    // frozen whenever the tick can't advance, so that timeout never elapses and
    // a wedged UART spins forever with IRQs off until the watchdog resets (the
    // frozen-tick bug class). The tick is frozen in an
    // exception (IPSR != 0; every peripheral IRQ sits above SysTick priority),
    // with interrupts masked (PRIMASK), or inside a FreeRTOS BASEPRI critical
    // section (BASEPRI masks SysTick). In those contexts use the count-bounded,
    // HAL-free WriteRaw() (the same exception-safe sink the crash path uses) so
    // logging can never hang us. LogBuffer above already captured the line.
    //
    // A suspended scheduler (vTaskSuspendAll()/osKernelLock(), e.g. a LOG_
    // reached from inside malloc via newlib's __malloc_lock) is treated the
    // same: the tick still runs there so HAL_UART_Transmit() would eventually
    // time out, but stalling every task for up to 1 s per line while nothing
    // can be scheduled is not acceptable - keep the whole-system stall bounded
    // by WriteRaw()'s sub-ms spins instead. (Pre-scheduler boot logging
    // returns taskSCHEDULER_NOT_STARTED and stays on the normal Write() leg.)
    if (__get_IPSR() != 0u || (__get_PRIMASK() & 0x1u) != 0u || __get_BASEPRI() != 0u ||
            xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) {
        Driver::LogUart::WriteRaw(data, len);
    } else {
        Driver::LogUart::Write(data, len);
    }

    // TODO: BLE forwarding must wait until BLE and settings are initialized;
    // this sink runs from t=0, so enabling it here is unsafe. Re-enable once a
    // readiness gate is in place.
    // WatchSettingsDebug settingsDebug = Settings::DebugManager::GetInstance().getSettings();
    // if (settingsDebug.bleCfg.useNUS && settingsDebug.logCfg.logToBLE) {
    //     BleLogger::GetInstance().write(data, len);
    // }
}

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
    // Reset of all peripherals, Initializes the Flash interface
    // and the Systick with default source.

    HAL_Init();

    // Enable instruction cache (default 2-ways set associative cache)
    HAL_ICACHE_Enable();

    // Enable IRQ Interrupts
    // They were disabled in the bootloader before jumping to the app.
    __enable_irq();

    // Configure the System Power
    SystemPower_Config();

    PriorityMap_init();

    // Configure the system clock. Switch Systick to LSE.
    Driver::System::Init();

    // Wire the crash dump module early so faults from here on are captured fully
    // (ranges/RTOS/clock are read in the fault path). The sink is only invoked
    // later, by CrashDump_check(), once the file system is up.
    CrashGlue_setup();

    // Bring the debug UART up now, off the fault path, so the crash-safe
    // LogUart::WriteRaw() used by the crash console path always has a configured
    // peripheral and never triggers heavy lazy HAL init from inside an exception.
    Driver::LogUart::Init();

    LOG_INIT(Kernel_logSink);
    LOG_INIT_TIMESTAMP(Driver::System::GetTimeMs);
    LOG_INFO_WP("\n");
    LOG_INFO("%s FW: %s\n", Driver::System::GetProjectName(),
                            Driver::System::GetBuildVersionStr());
    Driver::System::LogResetInfo();

    LOG_DEBUG("Core clock: %u\n", SystemCoreClock);
    LOG_DEBUG("LSE: %s\n", __HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) ? "ready" : "not ready");
    LOG_DEBUG("Bootloader version:  %s\n", Driver::System::GetBootVersionStr());
    LOG_DEBUG("LIBC version:  %s\n", Driver::System::GetLibcVersionStr());

    Driver::System::ClearResetFlags();

    // Start watchdog
#if (WDG_ON)
    Driver::Wdg::GetInstance().start(30000);
#else
    LOG_WARNING("WDG off!\n");
#endif

    // Set all pins to default state
    PinsDefaultConfig();


    // Initialize RTC
    Driver::Rtc &rtc = Driver::Rtc::GetInstance();
    rtc.init();

#if (USE_SYSTEM_SLEEP == 1)
    Driver::Sleep::Init(Driver::Rtc::GetInstance());
#if (SLEEP_ON)   // You can disable sleep here for debug
    Driver::Sleep::Enable();
#else
    LOG_WARNING("Sleep mode disabled!\n");
#endif
#endif // (USE_SYSTEM_SLEEP == 1)

    // Init scheduler
    osKernelInitialize();

    // Start background services
#if 1   // Debug heap in background
    HeapStackMonitor::Start(3000);  // Init timer for periodic check stack and heap
#endif
    Observer::Dispatcher::init();   // Observer task to call callbacks from interrupts

    osThreadAttr_t attr {};
    attr.name = "StartupTask";
    attr.stack_size = 40*1024;
    attr.priority = (osPriority_t)osPriorityNormal;
    osThreadNew(StartupTask, nullptr, &attr);

    // Start scheduler
    osKernelStart();

    // We should never get here as control is now taken by the scheduler
    Error_Handler();

    return -1;
}

/**
 * @brief   Logger filter installed while a DAM (Type-C debug-accessory)
 *          session routes the debug log out the connector: drop every
 *          Kernel-originated line and let application log lines through.
 *
 * The logger invokes the filter once with the global prefix (LOG_GLOBAL_PRX,
 * "Kernel" for kernel code) and once with the per-module prefix; suppressing
 * on the global prefix covers all kernel output regardless of which module
 * emitted it. Apps carry their own global prefix and are unaffected.
 *
 * @param   module  Prefix being checked (global or per-module).
 * @param   level   Numeric log level (unused).
 * @retval  'false' to suppress a Kernel line, 'true' to allow.
 */
static bool DamKernelLogFilter(const char *module, uint8_t level)
{
    (void)level;
    return !(module != nullptr && strcmp(module, LOG_GLOBAL_PRX) == 0);
}

class SysStateObserver :
        public Power::StatusCallback,
        public UsbDevice::Callback,
        public ErrorMonitor::Callback
{
public:
    SysStateObserver() = default;

    virtual void onUsbDetected(bool state) override
    {
        if (state) {
            Flash::GetInstance().powerModeAlwaysOn();

            // Do not bring up the USB stack while a protection fault or a
            // rapid-reconnect condition is active. A USB protection fault
            // means the data lines are isolated by NX20P, so enumeration
            // cannot succeed. (This only catches faults already flagged at
            // attach time; faults appearing later are handled reactively
            // in onSystemErrorSet.)
            ErrorMonitor& errorMonitor = ErrorMonitor::GetInstance();
            if (!errorMonitor.isSet(SystemError::CABLE_CONNECTION) &&
                !errorMonitor.isSet(SystemError::USB_PROTECTION)) {
                UsbDevice::GetInstance().onUsbDetected(true);
            }
        } else {
            Flash::GetInstance().powerModeAuto();

            UsbDevice::GetInstance().onUsbDetected(false);
        }
    }

    virtual void onUsbDam(bool active) override
    {
        // DAM (Type-C debug-accessory) routes the debug log out the
        // connector. 'active' is fault-gated in Power, so it is true only
        // while output is physically enabled. While active, filter out
        // Kernel log lines so only application output reaches the external
        // host; clearing the filter on exit restores full output. USB itself
        // stays hidden from the system in DAM (see Power::publishUsbState).
        //
        // Developer bypass: when logCfg.logInDam is set the kernel log is
        // kept fully enabled in DAM, so the filter is not installed.
#if UNA_DAM_HIDE_USB
        const bool allowInDam =
            Settings::DebugManager::GetInstance().getSettings().logCfg.logInDam;

        LOG_INIT_FILTER((active && !allowInDam) ? DamKernelLogFilter : nullptr);
#else
        // DAM USB-masking feature disabled at build time (UNA_DAM_HIDE_USB 0):
        // never filter the kernel log.
        (void)active;
#endif
    }

    virtual void onConnectedPC() override
    {
        Pmic::GetInstance().setChargeCurrent(BATTERY_CHARGE_CURRENT_PC);
    }

    virtual void onConnectedCharger() override
    {
        Pmic::GetInstance().setChargeCurrent(BATTERY_CHARGE_CURRENT);
    }

    virtual void onSystemErrorSet(SystemError error) override
    {
        // USB protection fault: data lines are isolated by NX20P, so tear
        // the USB stack down. Reactive path that also covers faults which
        // appear after enumeration already started.
        if (error == SystemError::USB_PROTECTION) {
            UsbDevice::GetInstance().onUsbDetected(false);
        }

        if (ErrorMonitor::GetInstance().hasNone()) {
            return;
        }
    }

    virtual void onSystemErrorClear(SystemError error) override
    {
        // USB protection fault cleared while the cable is still present:
        // bring the USB stack back up.
        if (error == SystemError::USB_PROTECTION &&
            Power::GetInstance().isUsbDetected()) {
            UsbDevice::GetInstance().onUsbDetected(true);
        }

        if (!ErrorMonitor::GetInstance().hasNone()) {
            return;
        }
    }

    virtual void onUsbOpenVCP() override
    {
        LOG_DEBUG("VCP opened\n");

        // Run CLI
        InstanceCLI::On();
    }
    virtual void onUsbCloseVCP() override
    {
        LOG_DEBUG("VCP closed\n");

        // Stop CLI
        InstanceCLI::Off();
    }

private:

};
static SysStateObserver sSysStateObserver;


/**
 * @brief Task to initialize base system components
 */
static void StartupTask(void *param)
{
    // Paint the MSP stack before anything else runs. This has to happen
    // from a task, not from main(): before the scheduler, main() itself runs on
    // MSP from _estack, and vPortStartFirstTask reloads MSP from VTOR[0] on the
    // way here - so painting earlier would either clobber live frames or
    // measure boot depth instead of ISR depth. First thing in the first task is
    // the earliest point the region is genuinely idle, which keeps the
    // watermark honest for everything that follows.
    Heap_paintIsrStack();

    // Then arm MSPLIM over the same region. Painting measures the stack;
    // this one bounds it, so an overflow becomes a fault with a backtrace
    // rather than a quiet write into the app image sitting below.
    Heap_armIsrStackLimit();

    TimePorting_init();

    InstanceTimerSysMs_init();

    Driver::Rtc &rtc = Driver::Rtc::GetInstance();
    Interface::IRtc::DateTime utcTime = rtc.get();
    std::time_t utc = std::time(nullptr);
    char buf[64] {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &utcTime);
    LOG_DEBUG("System UTC time:   %s (%u)\n", buf, static_cast<uint32_t>(utc));

    std::tm localTime {};
    localtime_r(&utc, &localTime);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);   // TODO: %z does not work
    const int offsetM = (rtc.getTimeZone() + rtc.getDstOffset()) / 60;
    const int offsetH = offsetM / 60;
    LOG_DEBUG("System Local time: %s %c%02d%02d\n", buf, offsetM > 0 ? '+' : '-', abs(offsetH), abs(offsetM % 60));

    // Initialize main I2C bus
    Interface::II2c &i2c = Instance::I2C::getInstance().getMainBus();

    // Start PMIC
    Pmic &pmic = Pmic::GetInstance();
    pmic.config(i2c, Driver::Gpio::GetInstance(Driver::Gpio::Pin::PWR_INT_L));
    const bool pmicOK = pmic.init();
    const bool usbDetected = pmic.isUsbDetected();

    const bool batteryDetected = pmic.isBatteryDetected();
    const bool batteryCharging = pmic.isCharging();

    Instance::UsbProtection::getInstance().init();

    // Initialize EMMC
    Flash& flash       = Flash::GetInstance();
    const bool flashOK = flash.init();
    flash_diskio_set_flash(flash);

#if 0 // Full erase. This may take more than an hour!
    uint32_t blcnt = flash.blockGetCount();
    for(uint32_t i = 0; i < blcnt; i++){
        LOG_INFO("Erase block %u / %u\n", i, blcnt);
        flash.blockErase(i);
    }
#endif

#if 0 // Fast erase. Just damage file system
    flash_diskio_erase(FLASH_DISK_UPD, 0, 1);
    flash_diskio_erase(FLASH_DISK_SYS, 0, 1);
    flash_diskio_erase(FLASH_DISK_GUI, 0, 1);
    flash_diskio_erase(FLASH_DISK_USR, 0, 1);
#endif

    // Initialize File System.
    //
    // Mount policy: the watch cannot boot with an unmounted volume, so every
    // volume must end up mounted. We still avoid reformatting on a *transient*
    // error that a retry can clear (the previous code wiped on any failure).
    // Format immediately only on a genuinely-blank volume (MountStatus::Blank);
    // on a disk error (MountStatus::DiskError - e.g. the eMMC still settling
    // right after a hard reset) retry a bounded number of times,
    // and only if it still fails format as a last resort so the watch boots.
    // A last-resort format wipes the volume, so it is logged loudly with the
    // reason - a field wipe is attributable, not silent. Booting on clean
    // volumes lets the mobile app repopulate the content it owns (module
    // identity, Apps, GPS EPO) to restore full functionality, which is only
    // possible if the watch actually boots.
    FS::FileSystem &fs = FS::FileSystem::GetInstance();
    using MountStatus = FS::FileSystem::MountStatus;
    bool fsOK = true;
    for (uint8_t i = 0; i < fs.GetVolumesNum(); i++) {
        MountStatus s = fs.mountStatus(i);

        if (s == MountStatus::Ok) {
            continue;
        }

        if (s == MountStatus::Blank) {
            // Genuinely blank (first boot / no valid filesystem): format it.
            LOG_WARNING("volume %u blank, formatting\n", i);
            fsOK &= fs.format(i);
            continue;
        }

        // Disk / transient error: retry with a short yielding settle delay
        // before resorting to a format (the eMMC is often still settling right
        // after a hard reset, which the delay rides out). A genuinely wedged
        // card is handled by the eMMC power-cycle recovery on the diskio->Flash
        // I/O path that mountStatus() exercises, rather than an explicit
        // re-init here - which is why the always-on flash power mode is only
        // entered after this loop: Flash::maybeRecover() is gated off while
        // always-on is set, and would otherwise never run at boot. Each failed
        // attempt logs the FatFs reason at WARNING inside FileSystem, so the
        // retries stay quiet unless we give up below.
        const uint8_t kMountRetries = 3;
        for (uint8_t attempt = 0;
                attempt < kMountRetries && s != MountStatus::Ok; attempt++) {
            OS::Delay(100);   // yield; runs in StartupTask with the scheduler up
            s = fs.mountStatus(i);
        }

        if (s == MountStatus::Ok) {
            continue;
        }

        // Still failing after retries. The watch cannot boot with this volume
        // unmounted, so format as a last resort to restore a mountable volume.
        // This wipes the volume - the single loud error here makes the wipe
        // attributable (the FatFs reason was logged by mountStatus() just above,
        // at WARNING), then the mobile app repopulates the volume's owned
        // content (module identity / Apps / GPS EPO) once the watch is up.
        LOG_ERROR("volume %u mount failed after %u retries - formatting to "
                "restore boot (volume wiped)\n", i, kMountRetries);
        fsOK &= fs.format(i);
    }

    // Keep the eMMC powered across the test-mode window (auto mode resumes
    // right after it, as before). Deliberately AFTER the mount loop:
    // always-on disables Flash::maybeRecover(), and the mount retries above
    // rely on that power-cycle recovery to clear a genuinely wedged card
    // instead of formatting it. USB/MSC raw host access cannot be active yet
    // (the USB stack is brought up further below), so a recovery power-cycle
    // during the mount loop is safe.
    flash.powerModeAlwaysOn();

    // Check if we need to go into test mode
    if (usbDetected) {
        Driver::SleepGuard guard;
        InstanceCLI::WaitTestMode(pmicOK && flashOK && fsOK);
    }

    flash.powerModeAuto();

    CrashDump_check();

    // Start FuelGauge
    FuelGauge &fuelGauge = FuelGauge::GetInstance();
    fuelGauge.config(i2c);
    fuelGauge.init();
    const float bateryVoltage   = fuelGauge.getBatteryVoltage();
    const float batteryLevel    = fuelGauge.getBatteryLevel();
    const float batteryRawLevel = fuelGauge.getRawBatteryLevel();
    const float batteryCapacity = fuelGauge.getCapacity();
    const float batteryFullCapacity = fuelGauge.getFullCapacity();

    // Battery detection sequence is not automatically triggered
    // when PMIC is powered only from battery (VBAT).
    if (usbDetected) {
        LOG_DEBUG("Running from USB\n");
    }

    if (!usbDetected || batteryDetected) {
         LOG_DEBUG("Battery: %.3fV, %.3f%% (real level %.3f%%), Capacity %.3f/%.3f mAh%s\n",
                 bateryVoltage,
                 batteryLevel,
                 batteryRawLevel,
                 batteryCapacity,
                 batteryFullCapacity,
                 (usbDetected & batteryCharging) ? ", charging" : "");
    } else if (!batteryDetected) {
        LOG_WARNING("No battery detected!\n");
    }

    // Settings
    Settings::Manager::GetInstance().load();
    Settings::LocalManager::GetInstance().load();
    Settings::DebugManager::GetInstance().load();
    Settings::AppSorting::GetInstance().load();

    // Optional system log to file (disabled by default; enabled via debug setting)
    if (Settings::DebugManager::GetInstance().getSettings().logCfg.logToFile) {
        Component::SystemLogger::GetInstance().start();
    }

    // At this point board without external modules, debug and backlight consumes 75uA

    // Initialize USB
    UsbDevice &usb = UsbDevice::GetInstance();
    usb.attachCallback(&sSysStateObserver);
    usb.init(false, FLASH_DISK_USR);    // VCP+MSC


    // Run Power manager
    Power &power = Power::GetInstance();
    power.attachStatusCallback(&sSysStateObserver);
    ErrorMonitor::GetInstance().attachStatusCallback(&sSysStateObserver);
    power.run();


    // Buttons
    HWButtons &buttons = HWButtons::GetInstance();
    buttons.config(
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW1),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::PWR_ON_1V8_L),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW3),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW4));
    buttons.init();

    // At this point board without external modules, debug and backlight consumes 78uA
    //Driver::Gpio::GetInstance(Driver::Gpio::Pin::BACKLIGHT_ON).init(Interface::IGpo::Type::OD, true); // disable backlight

    // Buzzer
    Buzzer &buzzer = Buzzer::GetInstance();
    buzzer.config(
            Driver::Pwm::GetInstance(Driver::Pwm::Port::PORT_1),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::BUZZER_EN1),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::BUZZER_EN2));
    buzzer.init();
    buzzer.enable();

    // Vibro
    Vibro &vibro = Vibro::GetInstance();
    vibro.config(i2c, Driver::Gpio::GetInstance(Driver::Gpio::Pin::HAPTIC_RST_N));
#if VIBRO_ON // For silent development
    vibro.init();
    vibro.enable();
#endif

    // Init additional components on external boards
    ComponentFactory &factory = ComponentFactory::GetInstance();
    factory.scanModules();

    // Backlight
    Interface::IBacklight &backlight = factory.getBacklight();
    backlight.init();
    backlight.on();  // Leave the backlight on

    // LCD
    Interface::ILcd &lcd = factory.getLcd();
    lcd.init();
    lcd.enable();
    if (!AppSurface::GetInstance().init(lcd)) {
        LOG_ERROR("App surface disabled: panel geometry mismatch\n");
    }

    // BLE
    Interface::IBle &ble = factory.getBle();
    ble.init();

#if (0) // Control point for measuring current consumption
    backlight.off();
    osThreadExit();
    // At this point board without external modules (except LCD), debug and backlight consumes 70 uA
    // Debug jumper: 270 uA
    // Backlight:    8.2 mA
    // BLE module:   120 uA
    // GPS module:
    // Altimeter:    5 uA
    // Bodysensor:
#endif

#if WORKMODE_ON
    // Start application
    Backend &backend = Backend::GetInstance();
    GuiBackendHolder::Set(backend);
    backend.run();

    if(factory.isLcdPresent()) {
        // Run TouchGFX
        MX_TouchGFX_Init();
    }
#else
    backlight.off();
    Test_execute();
#endif

    osThreadExit();
}

/**
 * @brief Power Configuration
 * @retval None
 */
static void SystemPower_Config(void)
{
    HAL_PWREx_EnableVddIO2();

    // Switch to SMPS regulator instead of LDO
    if (HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY) != HAL_OK) {
        Error_Handler();
    }
}

static void PinsDefaultConfig(void)
{
    using GPIO = Driver::Gpio;

    // Onboard pins

    // Main I2C
    GPIO::GetInstance(GPIO::Pin::MAIN_I2C_SCL).init(GPIO::Pull::NO_PULL);   // HW Pull-Up
    GPIO::GetInstance(GPIO::Pin::MAIN_I2C_SCL).init(GPIO::Pull::NO_PULL);   // HW Pull-Up

    // Buttons
    GPIO::GetInstance(GPIO::Pin::SW1).init(GPIO::Pull::UP);
    GPIO::GetInstance(GPIO::Pin::PWR_ON_1V8_L).init(GPIO::Pull::UP);
    GPIO::GetInstance(GPIO::Pin::SW3).init(GPIO::Pull::UP);
    GPIO::GetInstance(GPIO::Pin::SW4).init(GPIO::Pull::UP);

    // EMMC
    GPIO::GetInstance(GPIO::Pin::EMMC_EN).init(GPIO::Type::PP, false);
    GPIO::GetInstance(GPIO::Pin::SDIO_CK).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SDIO_CMD).init(GPIO::Pull::NO_PULL);   // HW Pull-Up
    GPIO::GetInstance(GPIO::Pin::SDIO_D0).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SDIO_D1).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SDIO_D2).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SDIO_D3).init(GPIO::Pull::NO_PULL);

    // Haptic
    GPIO::GetInstance(GPIO::Pin::HAPTIC_RST_N).init(Interface::IGpo::Type::PP, false);

    // Buzzer
    GPIO::GetInstance(GPIO::Pin::BUZZER_EN1).init(Interface::IGpo::Type::PP, false);
    GPIO::GetInstance(GPIO::Pin::BUZZER_EN2).init(Interface::IGpo::Type::PP, false);
    GPIO::GetInstance(GPIO::Pin::BUZZER_PWM).init(Interface::IGpo::Type::PP, false);

    // Fuel Gauge
    GPIO::GetInstance(GPIO::Pin::FUEL_GAUGE_ALRT).init(GPIO::Pull::NO_PULL);

    // PMIC
    GPIO::GetInstance(GPIO::Pin::PWR_INT_L).init(GPIO::Pull::NO_PULL);

    // USB Protection
    GPIO::GetInstance(GPIO::Pin::USB_FAULT_L).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::USB_DAM).init(GPIO::Type::PP, false);

    // USB
    GPIO::GetInstance(GPIO::Pin::USB_D_P).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::USB_D_N).init(GPIO::Pull::NO_PULL);

    // Debug TX
    //GPIO::GetInstance(GPIO::Pin::DEBUG_TX).init(GPIO::Pull::NO_PULL);



    // Connectors to Modules


    // Display Module Interface
    GPIO::GetInstance(GPIO::Pin::LCD_FRP).init(GPIO::Type::PP, false);
    GPIO::GetInstance(GPIO::Pin::LCD_XFRP).init(GPIO::Type::PP, true); // XFRP should be inverted to FRP
    GPIO::GetInstance(GPIO::Pin::LCD_QSPI_CS_VCK).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_QSPI_SCK_VST).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_QSPI_IO0_XRST).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_QSPI_IO1_HST).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_QSPI_IO2_ENB).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_QSPI_IO3_HCK).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_R1).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_R2).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_B1).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_B2).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_G1).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::LCD_G2).init(GPIO::Pull::NO_PULL);

    // Touch
    GPIO::GetInstance(GPIO::Pin::TOUCH_INT_L).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::TOUCH_RST_L).init(GPIO::Pull::NO_PULL);

    // Backlight
    GPIO::GetInstance(GPIO::Pin::BACKLIGHT_ON).init(GPIO::Type::OD, false); // Inverse logic. Turn on at start


    // Body Sensor Module Interface
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_INT0).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_INT1).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_INT2).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_INT3).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_RST_L).init(GPIO::Pull::NO_PULL); // HW Pull-Down in NCP110 (PPG_EN)
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_CS).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_SCK).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_MOSI).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_MISO).init(GPIO::Pull::NO_PULL);  // HW Pull-Down in NCP705 (VLED_EN)
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_SCL).init(GPIO::Pull::NO_PULL);   // HW Pull-Up
    GPIO::GetInstance(GPIO::Pin::SENSORBODY_SDA).init(GPIO::Pull::NO_PULL);   // HW Pull-Up

    // Altimeter Interface
    // AP7350 enable pin should be driven either high or low and must not be floating
    GPIO::GetInstance(GPIO::Pin::ALTIMETER_EN).init(GPIO::Type::PP, false);
    //GPIO::GetInstance(GPIO::Pin::ALTIMETER_INT).init(GPIO::Pull::NO_PULL); // USB_FAULT_L from board UNAcore 3.0

    // Sensor 1 Module Interface
    GPIO::GetInstance(GPIO::Pin::SENSOR1_TX_INT0).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR1_RX_INT1).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR1_INT2).init(GPIO::Pull::NO_PULL);
#if 0
    GPIO::GetInstance(GPIO::Pin::SENSOR1_RST_L).init(GPIO::Pull::NO_PULL);
#else
    // BLE OFF
    GPIO::GetInstance(GPIO::Pin::SENSOR1_RST_L).init(GPIO::Type::PP, false);
#endif
    GPIO::GetInstance(GPIO::Pin::SENSOR1_CS).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR1_SCK).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR1_MOSI).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR1_MISO).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR1_SCL).init(GPIO::Pull::NO_PULL);   // HW Pull-Up
    GPIO::GetInstance(GPIO::Pin::SENSOR1_SDA).init(GPIO::Pull::NO_PULL);   // HW Pull-Up


    // Sensor 2 Module Interface
    GPIO::GetInstance(GPIO::Pin::SENSOR2_TX_INT0).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR2_RX_INT1).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR2_INT2).init(GPIO::Pull::NO_PULL);
#if 0
    GPIO::GetInstance(GPIO::Pin::SENSOR2_RST_L).init(GPIO::Pull::NO_PULL);      // HW Pull-Up on GPS board (GNSS_EN)
#else
    // GPS OFF
    GPIO::GetInstance(GPIO::Pin::SENSOR2_RST_L).init(GPIO::Type::PP, false);    // HW Pull-Up on GPS board (GNSS_EN)
#endif
    GPIO::GetInstance(GPIO::Pin::SENSOR2_CS).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR2_SCK).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR2_MOSI).init(GPIO::Pull::NO_PULL);   // HW Pull-Down on GPS board (RTC_EINT)
    GPIO::GetInstance(GPIO::Pin::SENSOR2_MISO).init(GPIO::Pull::NO_PULL);
    GPIO::GetInstance(GPIO::Pin::SENSOR2_SCL).init(GPIO::Pull::NO_PULL);    // HW Pull-Up
    GPIO::GetInstance(GPIO::Pin::SENSOR2_SDA).init(GPIO::Pull::NO_PULL);    // HW Pull-Up


}

/**
 * @brief   This function is executed in case of error occurrence.
 */
void ErrorHandler(const char *file, uint32_t line)
{
    __disable_irq();

    LOG_ERROR("Error in file: %s, line: %d\n", file, line);

    // Some delay to print info
    int cnt = 100000;
    while (cnt-- > 0);

    Driver::System::Reset();
    while (1) {
    }
}

extern "C" void vApplicationIdleHook()
{
#if (WDG_ON)
    Driver::Wdg::GetInstance().update();
#endif
}

extern "C" void vApplicationStackOverflowHook(xTaskHandle xTask,
        char *pcTaskName)
{
    /* Run time stack overflow checking is performed if
     configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
     called if a stack overflow is detected. */
    LOG_ERROR("Stack overflow. Task: [%s]\n", pcTaskName);

    CrashDump_handlerStackOverflow(pcTaskName);
}

volatile TaskHandle_t lastSwitchedInTask = 0;
volatile TaskHandle_t lastSwitchedOutTask = 0;

extern "C" void vApplicationTaskSwitchedOutHook()
{
    lastSwitchedOutTask = xTaskGetCurrentTaskHandle();
}

extern "C" void vApplicationTaskSwitchedInHook()
{
    lastSwitchedInTask = xTaskGetCurrentTaskHandle();
}

#if (USE_SYSTEM_SLEEP == 0 && configUSE_TICKLESS_IDLE == 2 )
extern "C" void vPortSuppressTicksAndSleep(TickType_t xExpectedIdleTime){}
#endif // (USE_SYSTEM_SLEEP == 0)

extern "C" void __assert_func(const char *file, int line, const char *func,
        const char *failedexpr)
{
    // The crash module masks interrupts, latches the attribution, logs
    // file/line/func/expr via the console port, then forces the fault that
    // captures the full dump (udf -> HardFault -> reset).
    CrashDump_handlerAssert(file, static_cast<uint32_t>(line), func, failedexpr);

    // Unreachable, but required: it keeps the call above from being this
    // function's last instruction. Otherwise the call's return slot lands in the
    // next function and the offline unwinder mis-attributes the assert backtrace.
    while (1) {}
}

#ifdef  USE_FULL_ASSERT
/**
 * @brief   Reports the name of the source file and the source line number
 *          where the assert_param error has occurred.
 * @param   file: pointer to the source file name
 * @param   line: assert_param error line source number
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    ErrorHandler((const char *)file, line);
}
#endif /* USE_FULL_ASSERT */
