/**
 *******************************************************************************
 * @file    UtilBase.cpp
 * @brief   This file contains the CLI console Base utility
 *          implementation.
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "CLI/UtilBase.hpp"
#include <Porting.h>
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"
#include <string>
#include <cstdarg>
#include <memory>
#include <vector>

#include "TimerSysMs.h"

/**
 * @brief Construct a new Components::Cli Utils::Util Base object
 * 
 * @param utilName: Utility name 
 */
Components::CliUtils::UtilBase::UtilBase(const char* name) : mUtilName(name)
{}

/**
 * @brief  Returns the utility name
 * 
 * @return Utility name as const char* 
 */
const char* Components::CliUtils::UtilBase::getUtilName()
{
    return mUtilName.c_str();
}

/**
 * @brief  Returns the utility name string
 * 
 * @return Utility name as const std::string& 
 */
const std::string& Components::CliUtils::UtilBase::getUtilNameStr()
{
    return mUtilName;
}
