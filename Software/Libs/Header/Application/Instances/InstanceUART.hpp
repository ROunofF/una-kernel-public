#ifndef __INSTANCE_UART_HPP
#define __INSTANCE_UART_HPP

#include "Uart.hpp"
#include "LpUart.hpp"
#include "UartRtos.hpp"

namespace Instance {
    class UART
    {
    public:
        static UART& getInstance();

        Interface::IUart& getSensor1Bus();
        Interface::IUart& getSensor2Bus();

    private:
        UART();
        ~UART() = default;

        Driver::Uart&    mSensor1;
        Driver::UartRtos mSensor1Safe;
        Driver::LpUart&  mSensor2;
        Driver::UartRtos mSensor2Safe;
    };
}

#endif
