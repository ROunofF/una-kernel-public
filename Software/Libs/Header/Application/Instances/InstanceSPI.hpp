#ifndef __INSTANCE_SPI_HPP
#define __INSTANCE_SPI_HPP

#include "Spi.hpp"
#include "SpiRtos.hpp"
#include <cassert>

namespace Instance {
    class SPI
    {
    public:
        static Interface::ISpi& GetInstance(Driver::Spi::Port port)
        {
            static SPI sInstance;

            switch (port) {
                case Driver::Spi::Port::PORT_1: return sInstance.mPort1;
                case Driver::Spi::Port::PORT_2: return sInstance.mPort2;
                case Driver::Spi::Port::PORT_3: return sInstance.mPort3;
                default:
                    assert(false);
                    return sInstance.mPort1;
            }
        }

    private:
        SPI()
        : mPort1(&Driver::Spi::GetInstance(Driver::Spi::Port::PORT_1), 5000)
        , mPort2(&Driver::Spi::GetInstance(Driver::Spi::Port::PORT_2), 5000)
        , mPort3(&Driver::Spi::GetInstance(Driver::Spi::Port::PORT_3), 5000)
        {
        }

        ~SPI() = default;

        Driver::SpiRtos mPort1;
        Driver::SpiRtos mPort2;
        Driver::SpiRtos mPort3;
    };
}

#endif // __INSTANCE_SPI_HPP
