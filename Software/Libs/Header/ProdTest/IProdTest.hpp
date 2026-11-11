/**
 ******************************************************************************
 * @file    IProdTest.hpp
 * @brief   Interface for production tests: exposes a test name and a
 *          run(StringLogger&) entry point for capturing log history.
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_PROD_TEST_HPP
#define __INTERFACE_I_PROD_TEST_HPP

#include "ProdTest/Core/StringLogger.hpp"
#include "CLI/CLIParser.h"
#include "CLI/CLIInput.h"

#define TEST_DELAY  (1000)

namespace Interface {

    class IProdTest {
    public:
        IProdTest(const std::string& name) : mName(name)
        {}
        
        virtual ~IProdTest() = default;

        const std::string& getName()
        {
            return mName;
        }

        virtual bool run(StringLogger& slog,
                         CLIParser_t&  p,
                         CLIInput_t*   input) = 0;

    private:
        std::string mName;
    };

}

#endif  // __INTERFACE_I_PROD_TEST_HPP
