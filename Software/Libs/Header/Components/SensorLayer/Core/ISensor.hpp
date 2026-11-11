/**
 ******************************************************************************
 * @file    ISensor.hpp
 * @brief   ISensor interface
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __I_SENSOR_INTERFACE_HPP
#define __I_SENSOR_INTERFACE_HPP

namespace Interface
{
    class ISensor
    {
    public:
        virtual void sensorRefresh() = 0;
    }; /* class ISensor */
} /* namespace Sensor */

#endif /* __I_SENSOR_INTERFACE_HPP */
