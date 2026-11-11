/**
 ******************************************************************************
 * @file    pah831x_factory_test.h
 * @brief   Production-line tests of the PAH8316 and its LEDs, light path and
 *          interrupt lines.
 ******************************************************************************
 */

#ifndef PAH831X_FACTORY_TEST_H
#define PAH831X_FACTORY_TEST_H

#include "pah_plat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    Check_Brightness_PCB_Only_test_1_Side_A_2_LED,
    Check_Brightness_PCB_Only_test_2_Side_B_2_LED,
    Check_Brightness_PCB_Only_1_LED,
    INT_TEST,
    Check_Brightness_with_mechanical_cover_test_1_Side_A_2_LED,
    Check_Brightness_with_mechanical_cover_test_2_Side_B_2_LED,
    Check_Brightness_with_mechanical_cover_1_LED,
    Light_Leak_Test_2_LED,
    Light_Leak_Test_1_LED,
    Check_ECG_LOD_TEST,             ///< Not implemented: always fails
    Check_PPG_AFE_BUILT_IN_TEST,    ///< Not implemented: always fails
    Factory_Test_Stop,              ///< Powers the sensor down
} pah831x_factory_test_e;

/**
 * Platform hooks for the tests. The tests use plat's write, read and
 * delay_ms, and pass plat.user_data back as pArgs to every hook.
 */
typedef struct {
    pah_plat plat;
    /// Level of the sensor's interrupt line @p pin, 0 or 1. Used by INT_TEST,
    /// which needs both lines configured as inputs with pull-ups.
    bool   (*read_int)(uint8_t pin, void *pArgs);
} pah831x_factory_test_plat;

/**
 * @brief   Power up the sensor and read its product ID, 0x8311 for a PAH8316.
 *          Leaves the sensor powered up.
 */
void pah831x_factory_test_read_id(const pah831x_factory_test_plat *plat, uint16_t *id);

/**
 * @brief   Run one test, logging each measurement against its limits. Every
 *          implemented test powers the sensor down when it finishes.
 * @return  true if every measurement is within its limits; false also for
 *          a test that is not implemented
 */
bool pah831x_factory_test_run(const pah831x_factory_test_plat *plat, pah831x_factory_test_e test);

#ifdef __cplusplus
}
#endif

#endif /* PAH831X_FACTORY_TEST_H */
