/**
 ******************************************************************************
 * @file    GpsAiroha.hpp
 * @brief   GPS component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_GPS_AIROHA_HPP
#define __COMPONENT_GPS_AIROHA_HPP

#include "IGps.hpp"
#include "IUart.hpp"
#include "IGpio.hpp"

#include "OS.hpp"
#include "Observer.hpp"

#include "AG3335/AirohaAG3335M.hpp"
#include "AG3335/AirohaAiding.hpp"
#include "Sink/PushFileWriter.hpp"
#include "IImu.hpp"

namespace Gps
{

class GpsAiroha : public Interface::IGps,
                  public Airoha::AG3335M::RawDataCallback,
                  public Airoha::AG3335M::AidingCallback,
                  public Airoha::AG3335M::NmeaCallback,
                  public Interface::IImu::SensorCallback
{

public:


    GpsAiroha() = default;

    GpsAiroha(Interface::IUart &uart, Interface::IGpo &pwr);

    virtual ~GpsAiroha() = default;

    void config(Interface::IUart &uart, Interface::IGpo &pwr);

    void nmeaLogToFileEnable(bool state);

    virtual bool init() override;

    virtual bool deinit() override;

    virtual bool enable() override;

    virtual bool disable() override;

    virtual bool isEnabled() override;

    virtual void attachCallback(Interface::IGps::Callback *pCallback) override {
        mObserver.add(pCallback);
    }

    virtual void detachCallback(Interface::IGps::Callback *pCallback) override {
        mObserver.del(pCallback);
    }

    virtual void setPeriod(uint32_t seconds) override;

    virtual bool hasFix() override;

    virtual time_t getTime() override;

    float getSpeed() override;

    float getDistance() override;

    virtual Interface::IGps::LocationInfo getLocation() override;

    virtual Interface::IGps::FixMode getFixMode() override;

    virtual Interface::IGps::EpoPeriod getEpoFilePeriod() override;

    void setReferenceLocation(float lat, float lon, float alt)    override;
    bool getReferenceLocation(float& lat, float& lon, float& alt) override;

protected:

private:

    Airoha::AG3335M mDrv;
    Airoha::Aiding mAiding;

    std::time_t mTime = 0;
    Interface::IGps::LocationInfo mLocation {};
    Interface::IGps::FixMode mFixMode = Interface::IGps::FixMode::UNKNOWN;
    float mSpeed = 0.0f;
    float mDistance = 0.0f;
    uint32_t mDistanceGetTimestamp = 0;
    bool mL1Mode = false;

    Observer::Holder<Interface::IGps::Callback, 5> mObserver;

    bool mLogToFileEnable = false;
    Sink::PushFileWriter mLogToFile;

    bool mBinLogToFileEnable = false;
    Sink::PushFileWriter mBinLogToFile;

    bool mImuLogToFileEnable = false;
    Sink::PushFileWriter mImuLogToFile;
    Interface::IImu* mImu = nullptr;

    void resetState();

    virtual void onRawDataBin(uint8_t* data, size_t size) override;
    virtual void onRawDataRx(std::string_view rx) override;
    virtual void onRawTokensRx(std::string_view* tokens, size_t count) override;
    virtual void onRawDataTx(std::string_view tx) override;

    virtual void onNmeaGGA(const Airoha::Nmea::GGA& cmd) override;
    virtual void onNmeaRMC(const Airoha::Nmea::RMC& cmd) override;
    virtual void onNmeaGSV(const Airoha::Nmea::GSV& cmd) override;

    void onSyncRaw(const Interface::IImu::SyncSampleData* data, size_t count, float accelScale, float gyroScale,
                   uint32_t periodUs) override;

};

} // namespace Gps

#endif /* __COMPONENT_GPS_AIROHA_HPP */
