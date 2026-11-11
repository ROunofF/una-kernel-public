#define LOG_MODULE_PRX      "TestAirohaGPS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestAirohaGPS.hpp"

#include "AG3335/AirohaBase.hpp"
#include "AG3335/AirohaAG3335M.hpp"
#include "AG3335/AirohaAiding.hpp"

#include "Instances/InstanceUART.hpp"
#include "Uart.hpp"
#include "Gpio.hpp"
#include "Rtc.hpp"
#include "FileSystem.hpp"
#include "HeapStackMonitor.hpp"
#include "GpsAiroha.hpp"
#include "SwTimer.hpp"

class TestAirohaGPS :
                  public Airoha::AG3335M::RawDataCallback,
                  public Airoha::AG3335M::NmeaCallback
{
public:
    TestAirohaGPS(Airoha::AG3335M &d) : drv(d)
    {
        Interface::IUart &uart = Instance::UART::getInstance().getSensor2Bus();
        Interface::IGpo &pwr = Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_RST_L);

        drv.config(uart, pwr);
    }

    void run()
    {
        bool status;

        drv.setRawDataCallback(this);
        //drv.setPairCallback(this);
        drv.setNmeaCallback(this);

        status = drv.init();
        LOG_INFO("init() %s\n", status ? "OK" : "FAIL");

        OS::Delay(3000);

        status = drv.powerOn(true);
        LOG_INFO("powerOn(true) %s\n", status ? "OK" : "FAIL");

        status = drv.start(Airoha::StartType::HOT, true);
        LOG_INFO("start(HOT, true) %s\n", status ? "OK" : "FAIL");

        OS::Delay(2000);
        status = drv.enableOdometer(true);
        LOG_INFO("enableOdometer(true) %s\n", status ? "OK" : "FAIL");

        bool odStatus = false;
        float odDistance = 0.0f;
        status = drv.getOdometerStatus(odStatus, odDistance);
        LOG_INFO("getOdometerStatus() %s. %u %f\n", status ? "OK" : "FAIL", odStatus, odDistance);

        OS::Delay(2000);

        status = drv.getOdometerStatus(odStatus, odDistance);
        LOG_INFO("getOdometerStatus() %s. %u %f\n", status ? "OK" : "FAIL", odStatus, odDistance);


        OS::Delay(10000);

        status = drv.powerOff();
        LOG_INFO("powerOff() %s\n", status ? "OK" : "FAIL");
        status = drv.powerOff();

        OS::Delay(3000);

        status = drv.deinit();
        LOG_INFO("deinit() %s\n", status ? "OK" : "FAIL");
    }

    void printEpoTimeRange(Airoha::Aiding &provider)
    {

        Airoha::Aiding::EpoPeriod timeRange = provider.getEpoTimeRange();

        LOG_INFO("GPS:     %u - %u\n", timeRange.gps.start, timeRange.gps.end);
        LOG_INFO("GLONASS: %u - %u\n", timeRange.glonass.start, timeRange.glonass.end);
        LOG_INFO("GALILEO: %u - %u\n", timeRange.galileo.start, timeRange.galileo.end);
        LOG_INFO("BEIDOU:  %u - %u\n", timeRange.beidou.start, timeRange.beidou.end);
        LOG_INFO("QZSS:    %u - %u\n", timeRange.qzss.start, timeRange.qzss.end);
    }

private:
    Airoha::AG3335M &drv;

    virtual void onRawDataRx(std::string_view rx) override
    {
        LOG_DEBUG("[%.*s]\n", rx.size() - 2, rx.data()); // skip "\r\n" for pretty output
    }

    virtual void onRawTokensRx(std::string_view* tokens, size_t count) override
    {
        //LOG_DEBUG("called\n");
    }

    virtual void onRawDataTx(std::string_view tx) override
    {
        LOG_DEBUG("[%.*s]\n", tx.size() - 2, tx.data()); // skip "\r\n" for pretty output
    }

    virtual void onNmeaGGA(const Airoha::Nmea::GGA& cmd) override
    {
        LOG_DEBUG("GGA: Fix    %u, Lat %.6f, Lon %.6f, Alt %.1f m, HDOP %.1f, Sat: %d\n",
                cmd.fix_quality, cmd.latitude, cmd.longitude, cmd.altitude, cmd.hdop, cmd.num_satellites);
    }
    virtual void onNmeaRMC(const Airoha::Nmea::RMC& cmd) override
    {
        // Convert to UTC seconds
        uint32_t utc_sec = Airoha::Nmea::NmeaToUtcSeconds(cmd.utc_time, cmd.date);

        // Convert to std::tm
        std::tm utc_tm {};
        Airoha::Nmea::NmeaToTm(cmd.utc_time, cmd.date, &utc_tm);

        // Convert to m/s
        float speed = Airoha::Nmea::KnotsToMps(cmd.speed_knots);

        LOG_DEBUG("RMC: Status %u, UTC %u (%04u-%02u-%02u %02u:%02u:%02u), Speed %.1f m/s, Course %.1f degrees\n",
                cmd.status, utc_sec,
                utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
                utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec,
                speed, cmd.track_angle);
    }
    virtual void onNmeaGSV(const Airoha::Nmea::GSV& cmd) override
    {
        const size_t buffSize = 256;
        char buff[buffSize];
        size_t pos = 0;

        // Skip empty strings
        if (cmd.num_satellites == 0) {
            return;
        }

        switch (cmd.system) {
            case Airoha::Nmea::GnssSystem::GPS:
                pos += snprintf(buff + pos, buffSize - pos, "GSV [GPS    ]: ");
                break;
            case Airoha::Nmea::GnssSystem::GLONASS:
                pos += snprintf(buff + pos, buffSize - pos, "GSV [GLONASS]: ");
                break;
            case Airoha::Nmea::GnssSystem::GALILEO:
                pos += snprintf(buff + pos, buffSize - pos, "GSV [GALILEO]: ");
                break;
            case Airoha::Nmea::GnssSystem::BEIDOU:
                pos += snprintf(buff + pos, buffSize - pos, "GSV [BEIDOU ]: ");
                break;
            default:
                pos += snprintf(buff + pos, buffSize - pos, "GSV [UNKNOWN]: ");
                break;
        }

        // Access satellite data regardless of GNSS system
        for (uint8_t i = 0; i < cmd.num_satellites; i++) {
            pos += snprintf(buff + pos, buffSize - pos, "PRN=%2d, SNR=%2d. ",
                    cmd.satellites[i].prn, cmd.satellites[i].snr);
        }
        LOG_DEBUG("%s\n", buff);
    }


};

class TestAirohaGPS2
{
public:
    TestAirohaGPS2()
    {
        Interface::IUart &uart = Instance::UART::getInstance().getSensor2Bus();
        Interface::IGpo &pwr = Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_RST_L);

        drv = new (std::nothrow) Gps::GpsAiroha(uart, pwr);
        assert(drv);
    }

    void run()
    {
        bool status;

        status = drv->init();
        LOG_INFO("init() %s\n", status ? "OK" : "FAIL");

        status = drv->enable();
        LOG_INFO("enable() %s\n", status ? "OK" : "FAIL");

        OS::Delay(2000);
        float d = drv->getDistance();
        LOG_INFO("getDistance() %f\n", d);
        OS::Delay(2000);

        Driver::SwTimer t(30000);
        while(!t.expired()) {
            OS::Delay(2000);
            float d = drv->getDistance();
            LOG_INFO("Distance %f\n", d);
        }

        status = drv->disable();
        LOG_INFO("disable() %s\n", status ? "OK" : "FAIL");

        OS::Delay(2000);

        status = drv->deinit();
        LOG_INFO("deinit() %s\n", status ? "OK" : "FAIL");
    }

private:
    Gps::GpsAiroha *drv = nullptr;

};

static void HeapStackMonitorCheck()
{
    const uint32_t skTasksMax = 64;
    static TaskStatus_t mTaskInfoArray[skTasksMax];

    uint32_t osFreeHeap = HeapStackMonitor::GetOsHeapFree();
    LOG_WARNING("FreeRTOS Heap: %d/%d bytes (min ever %d bytes)\n",
                osFreeHeap, HeapStackMonitor::GetOsHeapSize(),
                HeapStackMonitor::GetOsHeapWaterMark());

    uint32_t sysFreeHeap = HeapStackMonitor::GetSysHeapFree();
    LOG_WARNING("System Heap: %d/%d bytes (min ever %d bytes)\n",
                sysFreeHeap, HeapStackMonitor::GetSysHeapSize(),
                HeapStackMonitor::GetSysHeapWaterMark());

    size_t tasksNum = HeapStackMonitor::GetTasksInfo(mTaskInfoArray, skTasksMax);
    bool breakLine = false;
    for (uint32_t i = 0; i < tasksNum; i++) {
        if (mTaskInfoArray[i].xHandle != nullptr) {
            LOG_WARNING("Task %-15s Free stack %d bytes\n",
                    mTaskInfoArray[i].pcTaskName,
                    mTaskInfoArray[i].usStackHighWaterMark * 4);
            breakLine = true;
        }
    }
    if (breakLine) {
        LOG_WARNING_WP("\n");
    }
}

void TestAirohaGPS_execute()
{
    LOG_INFO("start\n");

    uint32_t mems = 0;
    uint32_t meme = 0;

#if 0
    Airoha::Aiding epo {Driver::Rtc::GetInstance(), FS::FileSystem::GetInstance(), GPS_EPO_FILE_DIR_PATH};
    Airoha::AG3335M *drv = new Airoha::AG3335M;
    assert(drv);
    drv->setAidingCallback(&epo);
    TestAirohaGPS *test = new TestAirohaGPS(*drv);
    assert(test);


    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING Airoha::AG3335M\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        HeapStackMonitorCheck();

        mems = HeapStackMonitor::GetSysHeapFree();
        test->printEpoTimeRange(epo);
        meme = HeapStackMonitor::GetSysHeapFree();
        LOG_INFO("mem test->printEpoTimeRange(epo) = %d\n", meme - mems);

        mems = HeapStackMonitor::GetSysHeapFree();
        test->run();
        meme = HeapStackMonitor::GetSysHeapFree();
        LOG_INFO("mem test->run() = %d\n", meme - mems);

        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
#endif

#if 1
    TestAirohaGPS2 *test = new TestAirohaGPS2();
    assert(test);

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING Airoha::AG3335M\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        HeapStackMonitorCheck();

        mems = HeapStackMonitor::GetSysHeapFree();
        test->run();
        meme = HeapStackMonitor::GetSysHeapFree();
        LOG_INFO("mem test->run() = %d\n", meme - mems);

        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
#endif
}
