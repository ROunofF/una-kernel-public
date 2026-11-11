#ifndef __INSTANCE_PAH8316LS_HPP
#define __INSTANCE_PAH8316LS_HPP

#include "PAH8316LS.hpp"
#include "PAH8316LSFactoryTester.hpp"

namespace Instance {
    class PAH8316LS
    {
    public:
        static PAH8316LS& getInstance();

        Hardware::PAH8316LS&              getPAH8316LS();
        Hardware::PAH8316LSFactoryTester& getFactoryTester();

    private:
        PAH8316LS();
        ~PAH8316LS() = default;

        Hardware::PAH8316LS              mPAH8316LS;
        Hardware::PAH8316LSFactoryTester mFactoryTester;
    };
}

#endif
