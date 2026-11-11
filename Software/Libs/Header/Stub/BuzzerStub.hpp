/**
 ******************************************************************************
 * @file    BuzzerStub.hpp
 * @brief   Buzzer stub.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __BUZZER_STUB_HPP
#define __BUZZER_STUB_HPP

#include <cstdint>
#include <cstdbool>

#include "IBuzzer.hpp"

namespace Stub
{

class Buzzer : public Interface::IBuzzer {

public:

    Buzzer() = default;

    virtual ~Buzzer() = default;

    virtual bool play() override;

    virtual bool play(Note note) override;

    virtual bool play(const Note melody[], uint8_t len) override;

    virtual bool isPlaying() override;

    virtual void stop() override;

protected:

private:

};

} /* namespace Stub */

#endif /* __BUZZER_STUB_HPP */
