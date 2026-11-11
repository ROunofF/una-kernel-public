/**
******************************************************************************
 * @file    PAH8316LSFactoryTester.hpp
 * @brief   Factory tester for the PAH8316LS. The test procedures are in the
 *          PixArt PPG library; this class gives them the sensor's I2C bus,
 *          power and interrupt pins.
 *
 ******************************************************************************
 */

#ifndef PAH8316LS_FACTORY_TESTER_HPP
#define PAH8316LS_FACTORY_TESTER_HPP

#include "Porting.h"
#include "II2c.hpp"
#include "IGpio.hpp"

#include "pah831x_factory_test.h"

namespace Hardware {

class PAH8316LSFactoryTester
{
public:
    /// Default I2C address
    static const uint8_t skI2cAddr = 0x40;

    /**
     * @brief Constructor with I2C configuration.
     * @param i2c: Reference to the I2C object.
     * @param devAddr: Target device address. 7 bit, not shifted.
     */
    PAH8316LSFactoryTester(Interface::II2c& i2c,
                           Interface::IGpi& pinInt0,
                           Interface::IGpi& pinInt1,
                           Interface::IGpo& sensEn,
                           Interface::IGpo& ledEn,
                           uint8_t          devAddr = skI2cAddr);

    ~PAH8316LSFactoryTester();

    bool checkID();
    bool demo_factory_mode(pah831x_factory_test_e test_mode);

private:
    void init();
    void deinit();
    void enable(bool en);

    bool writeMem(uint8_t addr, const uint8_t *data, size_t len);
    bool readMem(uint8_t addr, uint8_t *data, size_t len);

    static pah_ret plat_write(uint8_t addr, const uint8_t *data, size_t write_size, void *pArgs);
    static pah_ret plat_read(uint8_t addr, uint8_t *data, size_t read_size, void *pArgs);
    static void    plat_delay_ms(uint32_t ms, void *pArgs);
    static bool    plat_read_int(uint8_t pin, void *pArgs);

    static constexpr uint32_t skI2cRetries = 3;

    Interface::II2c&          mI2C;
    Interface::IGpi&          mPinInt0;
    Interface::IGpi&          mPinInt1;
    Interface::IGpo&          mSensEn;
    Interface::IGpo&          mLedEn;
    uint8_t                   mAddr;
    bool                      mIsEnabled;
    pah831x_factory_test_plat mPlat;
};

} // Hardware

#endif // PAH8316LS_FACTORY_TESTER_HPP
