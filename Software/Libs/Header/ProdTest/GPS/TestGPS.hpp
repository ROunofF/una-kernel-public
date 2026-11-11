/**
 ******************************************************************************
 * @file    ProdTestGPS.hpp
 * @brief   Production test for the GPS module.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "IUart.hpp"
#include "IGpio.hpp"
#include "AG3335/AirohaAG3335M.hpp"
#include "ProdTest/Core/StringLogger.hpp"
#include "CLI/CLIConsole.h"
#include "CLI/CLIInput.h"
#include "HWButtons.hpp"

namespace ProdTest {

class TestGPS : public Airoha::AG3335M::RawDataCallback,
                public Airoha::AG3335M::NmeaCallback,
                public Interface::IButtons::Callback
{
public:
    TestGPS(Interface::IUart& uart,
            Interface::IGpo&  pwr,
            bool              resetByButton = false);
    ~TestGPS() override;

    bool run(StringLogger& slog, CLIInput_t* input);

    bool isExit();

private:
    bool testCommunication(StringLogger& slog);

    bool waitForSatellites(CLIInput_t* input);

    void onRawDataRx(std::string_view rx)        override;
    void onNmeaGSV(const Airoha::Nmea::GSV& cmd) override;

    void printStats(std::vector<Airoha::Nmea::SatelliteInfo>& sats);

    void onClick(Interface::IButtons::Id id) override;

    Interface::IUart&                        mUart;
    Interface::IGpo&                         mPwr;
    Airoha::AG3335M                          mAG3335M;
    uint32_t                                 mSuccessCounter = 0;
    std::vector<Airoha::Nmea::SatelliteInfo> mSatellites;
    const bool                               mResetByButton;
    bool                                     mExit;
};

}   // namespace ProdTest
