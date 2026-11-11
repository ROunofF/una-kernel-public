/**
 ******************************************************************************
 * @file    ProdTestVibro.hpp
 * @brief   Production test for the Vibro.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "Hardware/BMI270/BMI270.hpp"
#include "IVibro.hpp"

namespace ProdTest {

class Vibro : public Interface::IProdTest {
public:
    Vibro(const std::string& id);
    ~Vibro() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    void startVibro(StringLogger& slog, int argc, const char** argv);
    void startVerify(StringLogger& slog, int argc, const char** argv);
    void checkCommand(StringLogger& slog, CLIInput_t* input);

    bool measuring(StringLogger&     slog,
                   Hardware::BMI270& imu,
                   float             magnitudeThreshold,
                   uint32_t          period,
                   float&            maxMagnitude,
                   uint32_t&         duration,
                   bool              showXLSamples);

    bool startImu(StringLogger& slog, Hardware::BMI270& imu);
    void stopImu(Hardware::BMI270& imu);

    void startVibro();
    void stopVibro();

    Interface::IVibro::Note mMelody[10];
};

}   // namespace ProdTest
