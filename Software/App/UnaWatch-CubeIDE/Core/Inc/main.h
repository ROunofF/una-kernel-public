/**
 ******************************************************************************
 * @file    main.h
 * @brief   Header for main.c file. This file contains the common defines of
 *          the application.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32u5xx_hal.h"

void ErrorHandler(const char *file, uint32_t line);
#define Error_Handler()   ErrorHandler(__FILE_NAME__, __LINE__)

// The macros are designed for 1024Hz as they use shifts
// instead of multiplication and division.
#define HAL_MS_TO_TICKS_MAX_MS  4194303999UL

#define HAL_TICKS_TO_MS( ticks ) \
( (uint32_t)( ( (uint64_t)ticks * 1000ULL ) >> 10 ) )

#define HAL_MS_TO_TICKS( ms ) \
( (((uint32_t)(ms)) > HAL_MS_TO_TICKS_MAX_MS) ? 0xFFFFFFFFU : ( (uint32_t)( ( (uint64_t)ms << 10 ) / 1000ULL ) ) )


// Pins
#define HSE_CLK_IN_Pin              GPIO_PIN_0
#define HSE_CLK_IN_GPIO_Port        GPIOH
#define HSE_CLK_OUT_Pin             GPIO_PIN_1
#define HSE_CLK_OUT_GPIO_Port       GPIOH

#define CLK_32K_IN_Pin              GPIO_PIN_14
#define CLK_32K_IN_GPIO_Port        GPIOC
#define CLK_32K_OUT_Pin             GPIO_PIN_15
#define CLK_32K_OUT_GPIO_Port       GPIOC
#define CLK_32K_SRC_TYPE            RCC_LSE_BYPASS

#define SWO_Pin                     GPIO_PIN_3
#define SWO_GPIO_Port               GPIOB
#define SWDIO_Pin                   GPIO_PIN_13
#define SWDIO_GPIO_Port             GPIOA
#define SWCLK_Pin                   GPIO_PIN_14
#define SWCLK_GPIO_Port             GPIOA

#define MAIN_SCL_Pin                GPIO_PIN_6
#define MAIN_SCL_GPIO_Port          GPIOB
#define MAIN_SDA_Pin                GPIO_PIN_7
#define MAIN_SDA_GPIO_Port          GPIOB

#define SW1_Pin                     GPIO_PIN_4
#define SW1_GPIO_Port               GPIOD
#define PWR_ON_1V8_L_Pin            GPIO_PIN_15
#define PWR_ON_1V8_L_GPIO_Port      GPIOD
#define SW3_Pin                     GPIO_PIN_5
#define SW3_GPIO_Port               GPIOA
#define SW4_Pin                     GPIO_PIN_8
#define SW4_GPIO_Port               GPIOD

#define EMMC_EN_Pin                 GPIO_PIN_10
#define EMMC_EN_GPIO_Port           GPIOD
#define SDIO_CK_Pin                 GPIO_PIN_12
#define SDIO_CK_GPIO_Port           GPIOC
#define SDIO_CMD_Pin                GPIO_PIN_2
#define SDIO_CMD_GPIO_Port          GPIOD
#define SDIO_D0_Pin                 GPIO_PIN_8
#define SDIO_D0_GPIO_Port           GPIOC
#define SDIO_D1_Pin                 GPIO_PIN_9
#define SDIO_D1_GPIO_Port           GPIOC
#define SDIO_D2_Pin                 GPIO_PIN_10
#define SDIO_D2_GPIO_Port           GPIOC
#define SDIO_D3_Pin                 GPIO_PIN_11
#define SDIO_D3_GPIO_Port           GPIOC

#define HAPTIC_RST_N_Pin            GPIO_PIN_2
#define HAPTIC_RST_N_GPIO_Port      GPIOF

#define BUZZER_EN1_Pin              GPIO_PIN_9
#define BUZZER_EN1_GPIO_Port        GPIOB
#define BUZZER_EN2_Pin              GPIO_PIN_5
#define BUZZER_EN2_GPIO_Port        GPIOD
#define BUZZER_PWM_Pin              GPIO_PIN_8
#define BUZZER_PWM_GPIO_Port        GPIOB

#define FUEL_GAUGE_ALRT_Pin         GPIO_PIN_6
#define FUEL_GAUGE_ALRT_GPIO_Port   GPIOC

#define PWR_INT_L_Pin               GPIO_PIN_7
#define PWR_INT_L_GPIO_Port         GPIOG

#define USB_FAULT_L_Pin             GPIO_PIN_0
#define USB_FAULT_L_GPIO_Port       GPIOF
#define USB_DAM_Pin                 GPIO_PIN_15
#define USB_DAM_GPIO_Port           GPIOB

#define USB_D_P_Pin                 GPIO_PIN_12
#define USB_D_P_GPIO_Port           GPIOA
#define USB_D_N_Pin                 GPIO_PIN_11
#define USB_D_N_GPIO_Port           GPIOA

// Debug UART interface configuration
#define DEBUG_INSTANCE              USART3
#define DEBUG_RCC_PERIPHCLK         RCC_PERIPHCLK_USART3
#define DEBUG_RCC_CLKSOURCE         RCC_USART3CLKSOURCE_HSI
#define DEBUG_RCC_CLK_ENABLE_FUNC   __HAL_RCC_USART3_CLK_ENABLE
#define DEBUG_RCC_CLK_DISABLE_FUNC  __HAL_RCC_USART3_CLK_DISABLE
#define DEBUG_GPIO_CLK_ENABLE_FUNC  __HAL_RCC_GPIOC_CLK_ENABLE
#define DEBUG_GPIO_CLK_DISABLE_FUNC __HAL_RCC_GPIOC_CLK_DISABLE
#define DEBUG_TX_Pin                GPIO_PIN_4
#define DEBUG_TX_GPIO_Port          GPIOC
#define DEBUG_TX_GPIO_ALT           GPIO_AF7_USART3

#define LCD_FRP_Pin                 GPIO_PIN_2
#define LCD_FRP_GPIO_Port           GPIOB
#define LCD_XFRP_Pin                GPIO_PIN_14
#define LCD_XFRP_GPIO_Port          GPIOD
#define LCD_QSPI_CS_VCK_Pin         GPIO_PIN_4
#define LCD_QSPI_CS_VCK_GPIO_Port   GPIOA
#define LCD_QSPI_SCK_VST_Pin        GPIO_PIN_10
#define LCD_QSPI_SCK_VST_GPIO_Port  GPIOB
#define LCD_QSPI_IO0_XRST_Pin       GPIO_PIN_1
#define LCD_QSPI_IO0_XRST_GPIO_Port GPIOB
#define LCD_QSPI_IO1_HST_Pin        GPIO_PIN_0
#define LCD_QSPI_IO1_HST_GPIO_Port  GPIOB
#define LCD_QSPI_IO2_ENB_Pin        GPIO_PIN_7
#define LCD_QSPI_IO2_ENB_GPIO_Port  GPIOA
#define LCD_QSPI_IO3_HCK_Pin        GPIO_PIN_6
#define LCD_QSPI_IO3_HCK_GPIO_Port  GPIOA
#define LCD_R1_Pin                  GPIO_PIN_0
#define LCD_R1_GPIO_Port            GPIOE
#define LCD_R2_Pin                  GPIO_PIN_8
#define LCD_R2_GPIO_Port            GPIOE
#define LCD_B1_Pin                  GPIO_PIN_4
#define LCD_B1_GPIO_Port            GPIOE
#define LCD_B2_Pin                  GPIO_PIN_12
#define LCD_B2_GPIO_Port            GPIOE
#define LCD_G1_Pin                  GPIO_PIN_2
#define LCD_G1_GPIO_Port            GPIOE
#define LCD_G2_Pin                  GPIO_PIN_10
#define LCD_G2_GPIO_Port            GPIOE

#define TOUCH_INT_L_Pin             GPIO_PIN_13
#define TOUCH_INT_L_GPIO_Port       GPIOC
#define TOUCH_RST_L_Pin             GPIO_PIN_1
#define TOUCH_RST_L_GPIO_Port       GPIOF

#define BACKLIGHT_ON_Pin            GPIO_PIN_3
#define BACKLIGHT_ON_GPIO_Port      GPIOF

#define SENSORBODY_INT0_GPIO_Port   GPIOB
#define SENSORBODY_INT0_Pin         GPIO_PIN_11
#define SENSORBODY_INT1_GPIO_Port   GPIOF
#define SENSORBODY_INT1_Pin         GPIO_PIN_12
#define SENSORBODY_INT2_GPIO_Port   GPIOF
#define SENSORBODY_INT2_Pin         GPIO_PIN_14
#define SENSORBODY_INT3_Pin         GPIO_PIN_6
#define SENSORBODY_INT3_GPIO_Port   GPIOG
#define SENSORBODY_RST_L_Pin        GPIO_PIN_13
#define SENSORBODY_RST_L_GPIO_Port  GPIOF
#define SENSORBODY_CS_Pin           GPIO_PIN_12
#define SENSORBODY_CS_GPIO_Port     GPIOG
#define SENSORBODY_SCK_Pin          GPIO_PIN_9
#define SENSORBODY_SCK_GPIO_Port    GPIOG
#define SENSORBODY_MOSI_Pin         GPIO_PIN_6
#define SENSORBODY_MOSI_GPIO_Port   GPIOD
#define SENSORBODY_MISO_Pin         GPIO_PIN_10
#define SENSORBODY_MISO_GPIO_Port   GPIOG
#define SENSORBODY_SCL_Pin          GPIO_PIN_13
#define SENSORBODY_SCL_GPIO_Port    GPIOB
#define SENSORBODY_SDA_Pin          GPIO_PIN_14
#define SENSORBODY_SDA_GPIO_Port    GPIOB

#define ALTIMETER_EN_Pin            GPIO_PIN_0
#define ALTIMETER_EN_GPIO_Port      GPIOG
//#define ALTIMETER_INT_Pin           GPIO_PIN_0 // USB_FAULT_L from board UNAcore 3.0
//#define ALTIMETER_INT_GPIO_Port     GPIOF

#define SENSOR1_TX_INT0_Pin         GPIO_PIN_9
#define SENSOR1_TX_INT0_GPIO_Port   GPIOA
#define SENSOR1_RX_INT1_Pin         GPIO_PIN_10
#define SENSOR1_RX_INT1_GPIO_Port   GPIOA
#define SENSOR1_INT2_Pin            GPIO_PIN_0
#define SENSOR1_INT2_GPIO_Port      GPIOA
#define SENSOR1_RST_L_Pin           GPIO_PIN_8
#define SENSOR1_RST_L_GPIO_Port     GPIOA
#define SENSOR1_CS_Pin              GPIO_PIN_15
#define SENSOR1_CS_GPIO_Port        GPIOA
#define SENSOR1_SCK_Pin             GPIO_PIN_2
#define SENSOR1_SCK_GPIO_Port       GPIOG
#define SENSOR1_MOSI_Pin            GPIO_PIN_5
#define SENSOR1_MOSI_GPIO_Port      GPIOB
#define SENSOR1_MISO_Pin            GPIO_PIN_3
#define SENSOR1_MISO_GPIO_Port      GPIOG
#define SENSOR1_SCL_Pin             GPIO_PIN_0
#define SENSOR1_SCL_GPIO_Port       GPIOC
#define SENSOR1_SDA_Pin             GPIO_PIN_1
#define SENSOR1_SDA_GPIO_Port       GPIOC

#define SENSOR2_TX_INT0_Pin         GPIO_PIN_2
#define SENSOR2_TX_INT0_GPIO_Port   GPIOA
#define SENSOR2_RX_INT1_Pin         GPIO_PIN_3
#define SENSOR2_RX_INT1_GPIO_Port   GPIOA
#define SENSOR2_INT2_Pin            GPIO_PIN_1
#define SENSOR2_INT2_GPIO_Port      GPIOA
#define SENSOR2_RST_L_Pin           GPIO_PIN_5
#define SENSOR2_RST_L_GPIO_Port     GPIOG
#define SENSOR2_CS_Pin              GPIO_PIN_12
#define SENSOR2_CS_GPIO_Port        GPIOB
#define SENSOR2_SCK_Pin             GPIO_PIN_1
#define SENSOR2_SCK_GPIO_Port       GPIOD
#define SENSOR2_MOSI_Pin            GPIO_PIN_3
#define SENSOR2_MOSI_GPIO_Port      GPIOC
#define SENSOR2_MISO_Pin            GPIO_PIN_2
#define SENSOR2_MISO_GPIO_Port      GPIOC
#define SENSOR2_SCL_Pin             GPIO_PIN_12
#define SENSOR2_SCL_GPIO_Port       GPIOD
#define SENSOR2_SDA_Pin             GPIO_PIN_13
#define SENSOR2_SDA_GPIO_Port       GPIOD


#ifndef PROJECT_NAME
#define PROJECT_NAME        "UNA Watch"
#endif

#ifndef BUILD_VERSION
#define BUILD_VERSION       "0.0.0"
#endif

#define USE_SYSTEM_SLEEP    (1)

// Design battery capacity in mAh.
#define BATTERY_CAPACITY        (200)

// Battery charge current in mA
#define BATTERY_CHARGE_CURRENT  (150)

// Battery charge current from PC, in mA
#define BATTERY_CHARGE_CURRENT_PC  (150)

// Battery max charge voltage, mV
#define POWER_BAT_VOLTAGE_MAX   (4400)  // Step is 20mV. Max 4500!

// Fuel-gauge EZ-model tuning (MAX17262)
#define BATTERY_TERM_CURRENT       (14)    // mA, IChgTerm
#define BATTERY_EMPTY_VOLTAGE      (3400)  // mV, VEmpty.VE (10 mV LSB)
#define BATTERY_RECOVERY_VOLTAGE   (3960)  // mV, VEmpty.VR (40 mV LSB)

// Power rails configuration in mV
#define POWER_RAIL_MV_SW1       (1200)
#define POWER_RAIL_MV_SW2       (1800)
#define POWER_RAIL_MV_LDO1      (1800)
#define POWER_RAIL_MV_LDO2      (3200)


// FS and HS identification 0 - FS, 1 - HS
#define USB_SPEED_HS            (1)

#define USB_CDC_ENABLE          (1)
#define USB_MSC_ENABLE          (1)

// Vibro
#define VIBRO_USE_SETTINGS_IN_RTC (1)


// BLE

#define MANUFACTURER_NAME               "UNA"
#define MODEL_NAME                      "UNA Watch"

// d'193 - Sports Watch
#define BLE_DEVICE_APPEARANCE           (0x00C1)

// Company identifier code (Default is 0x0030 - STMicroelectronics)
#define BLE_MANUFACTURER_ID             (0x0030)

// Manufacturer "Magic string" for device recognition
#define BLE_MANUFACTURER_MAGIC_STR      "UNAWATCH"

#define BOOTLOADER_OTA_PATH                 "0:/ble.ota" // Real name doesn't matter.
#define USER_ROOT_DIR                       "2:"
#define BLE_ROOT_DIR                        "2:"
#define SETTINGS_FILE_FULL_PATH             "2:/settings.json"
#define SETTINGS_LOCAL_FILE_FULL_PATH       "2:/local_settings.json"
#define SETTINGS_DEBUG_FILE_FULL_PATH       "2:/debug_settings.json"
#define SETTINGS_APPSORTING_FILE_FULL_PATH  "2:/Apps/app_sorting.json"
#define SETTINGS_ACCESSORIES_FILE_FULL_PATH "2:/accessories.json"
#define UPDATE_DIR_PATH                     "2:/Update/"
#define OTA_FILE_EXTENSION                  ".ota"
#define APPS_DIR_PATH                       "2:/Apps/"
#define APP_FILE_EXTENSION                  ".uapp"
#define APPS_LIST_FILE_PATH                 "2:/Apps/app_list.json"
#define APPS_ACTIVITY_FILE_REG_PATH         "2:/Apps/latest_activity.txt"
#define APP_ACTIVITY_FILE_EXTENSION         ".fit"
#define GPS_NMEA_LOG_DIR_PATH               "2:/Log/GPS/"
#define PIXART_LOG_DIR_PATH                 "2:/Log/PPG/"
#define BATTERY_LOG_DIR_PATH                "2:/Log/Battery/"
#define SYSTEM_LOG_DIR_PATH                 "2:/Log/System/"
// Structured workouts, written by the phone or over USB; apps read them as
// ":/Workouts" (read-only, see GuardPath.hpp).
#define WORKOUTS_SCHEDULE_DIR_PATH          "2:/Workouts/Schedule"

// GPS
#define GPS_EPO_FILE_DIR_PATH           "2:/GPS_EPO/"
#define PS_EPO_FILE_EXTENSION           ".DAT"

// DailyHealth
#define DAILY_HEALTH_DIR_PATH           "2:/DailyHealth/"
#define DAILY_HEALTH_TMP_FULL_PATH      "2:/DailyHealth/dh.tmp"

// Crash dump
#define CRASH_DUMP_DIR_PATH             "2:/Crash/"

#define MODULE_INFO_CORE_FILE_FULL_PATH "1:/modinfo_core.bin"

#define LCD_WIDTH   240
#define LCD_HEIGHT  240

// RTC backup registers
#define RTC_BRK_REG_TIME                (0)     // RTC_BKP_DR0
#define RTC_BRK_REG_TIMEZONE            (1)     // RTC_BKP_DR1
#define RTC_BRK_REG_DST                 (2)     // RTC_BKP_DR2
#define RTC_BRK_REG_POWER_OFF_FLAG      (3)     // RTC_BKP_DR3
//#define RTC_BRK_REG_                    (4)     // RTC_BKP_DR4
#define RTC_BRK_REG_VIBRO_CFG           (5)     // RTC_BKP_DR5
#define RTC_BRK_REG_GPS_REF_UTC         (6)     // RTC_BKP_DR6
#define RTC_BRK_REG_GPS_REF_LAT         (7)     // RTC_BKP_DR7
#define RTC_BRK_REG_GPS_REF_LON         (8)     // RTC_BKP_DR8
#define RTC_BRK_REG_GPS_REF_ALT         (9)     // RTC_BKP_DR9
#define RTC_BRK_REG_GPS_REF_CRC         (10)    // RTC_BKP_DR10 (used copy of UTC instead real CRC)

#define RTC_BRK_REG_WATCH_ALTITUDE      (11)    // RTC_BKP_DR11
#define RTC_BRK_REG_WATCH_ALTITUDE_CRC  (12)    // RTC_BKP_DR12 (CRC32)

// Magnetometer hard- and soft-iron correction: ten payload words, then their
// CRC in the register just past them. The store is given the first register
// and derives the rest, so these have to stay in this order and adjacent; the
// calibration instance static_asserts that against this map.
#define RTC_BRK_REG_MAG_CAL_OFFSET_X    (13)    // RTC_BKP_DR13
#define RTC_BRK_REG_MAG_CAL_OFFSET_Y    (14)    // RTC_BKP_DR14
#define RTC_BRK_REG_MAG_CAL_OFFSET_Z    (15)    // RTC_BKP_DR15
#define RTC_BRK_REG_MAG_CAL_M00         (16)    // RTC_BKP_DR16
#define RTC_BRK_REG_MAG_CAL_M01         (17)    // RTC_BKP_DR17
#define RTC_BRK_REG_MAG_CAL_M02         (18)    // RTC_BKP_DR18
#define RTC_BRK_REG_MAG_CAL_M11         (19)    // RTC_BKP_DR19
#define RTC_BRK_REG_MAG_CAL_M12         (20)    // RTC_BKP_DR20
#define RTC_BRK_REG_MAG_CAL_M22         (21)    // RTC_BKP_DR21
#define RTC_BRK_REG_MAG_CAL_RADIUS      (22)    // RTC_BKP_DR22
#define RTC_BRK_REG_MAG_CAL_CRC         (23)    // RTC_BKP_DR23 (CRC32)

// The last 128 bytes of the shared lib space
#define LIBC_INFO_BLOCK_ADDR            (0x0805FF80)
#define BOOT_INFO_BLOCK_ADDR            (0x08027F80)

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
