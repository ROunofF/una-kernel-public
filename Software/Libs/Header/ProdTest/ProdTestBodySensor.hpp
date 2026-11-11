/**
 ******************************************************************************
 * @file    ProdTestBodySensor.hpp
 * @brief   Production test for the BodySensor.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "IBodySensor.hpp"
#include "PAH8316LS.hpp"

namespace ProdTest {

class BodySensor : public Interface::IProdTest,
                   virtual public Interface::IBodySensor::Callback
{
public:
    BodySensor(const std::string& id);
    ~BodySensor() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    bool run(StringLogger& slog, Hardware::PAH8316LS& bodySensor);

    void onTouch(bool val)                                            override;
    void onHeartRate(uint8_t hr, uint8_t trustLevel)                  override;

    bool    mTouched;
    bool    mDataValid;
    uint8_t mHr;
    uint8_t mTrustLevel;
};

}   // namespace ProdTest
