/**
 ******************************************************************************
 * @file    VibroStub.hpp
 * @brief   Vibro stub.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __VIBRO_STUB_HPP
#define __VIBRO_STUB_HPP

#include <cstdint>
#include <cstdbool>

#include "IVibro.hpp"

namespace Stub
{

class Vibro : public ::Interface::IVibro {

public:

    Vibro() = default;

    virtual ~Vibro() = default;

    virtual void play(uint8_t effect = Effect::STRONG_CLICK_100) override;

    virtual void play(const Note melody[], uint8_t len, uint8_t loop = 0) override;

    virtual bool isPlaying() override;

    virtual void stop() override;

protected:

private:

};

} /* namespace Stub */

#endif /* __VIBRO_STUB_HPP */
