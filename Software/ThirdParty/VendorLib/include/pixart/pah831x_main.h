/**
 ******************************************************************************
 * @file    pah831x_main.h
 * @brief   Interface to the PAH831x PPG sensor driver in the PixArt PPG
 *          library: start and stop the sensor, service its interrupt, and
 *          collect batches of PPG samples.
 ******************************************************************************
 */

#ifndef PAH831X_MAIN_H
#define PAH831X_MAIN_H

#include "pah_plat.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Auto-exposure state, as read back from the chip
typedef struct {
    uint32_t    Expo_time_A;
    uint32_t    Expo_time_B;
    uint32_t    Expo_time_C;
    uint32_t    Expo_time_D;
    uint32_t    touch_data;
    uint8_t     LEDDAC_A;
    uint8_t     LEDDAC_B;
    uint8_t     LEDDAC_C;
    uint8_t     LEDDAC_D;
    int32_t     cap_touch_data;
} pah_ae_info;

/**
 * One batch of PPG samples, from get_ppg_data_out(). The pointers refer to
 * the driver's own buffers, which the next batch overwrites.
 */
typedef struct {
    uint64_t    end_timestamp;      ///< Tick of the batch's last sample
    uint64_t    start_timestamp;    ///< Tick of the batch's first sample
    uint8_t     ppg_data_ready;
    uint32_t    duration;           ///< Length of the batch, ms
    uint8_t     is_touched;
    uint8_t     ch_num;             ///< Channels, interleaved in ppg_raw_data
    uint32_t    sample_num_per_ch;
    uint8_t    *ppg_raw_data;       ///< ch_num * sample_num_per_ch 32-bit samples
    uint8_t    *ppg_dac_data;       ///< One LED DAC value per sample
    uint16_t   *ppg_adc_data;
    uint32_t    touch_value;
    uint8_t     afe_gain_flag;
} ppg_data_out;

typedef struct {
    uint64_t    end_timestamp;
    uint32_t    duration;
    uint8_t     is_touched;
    uint8_t     ch_num;
    uint8_t     acc_data_num;
    uint32_t    sample_num_per_ch;
    uint32_t    ppg_raw_data[55];
    uint8_t     ppg_dac_data[6];
    int16_t     acc_data[170];
    uint32_t    frame_count;
    uint8_t     afe_gain_flag;
} ppg_data_out_buffer;

/// One batch of ECG samples, from get_ecg_data_out()
typedef struct {
    uint64_t    end_timestamp;
    uint64_t    start_timestamp;
    uint8_t     ecg_data_ready;
    uint32_t    duration;
    uint8_t     is_touched;
    uint8_t     ch_num;
    uint32_t    sample_num_per_ch;
    uint8_t    *ecg_raw_data;
    float      *ecg_raw_data_mv;
    uint8_t    *ecg_tag_data;
} ecg_data_out;

/// How the driver learns that a batch is ready
typedef enum {
    PAH831X_WORK_IDLE = 0,
    PAH831X_WORK_DRI,       ///< The chip's data-ready interrupt
    PAH831X_WORK_POLLING,   ///< Polling the FIFO
} pah831x_work_mode;

/// When the PPG runs
typedef enum {
    PAH831X_INIT_IDLE = 0,
    PAH831X_INIT_AUTO_MODE,     ///< Only while the sensor detects skin contact
    PAH831X_INIT_ALWAYS_RUN,    ///< Continuously
    PAH831X_INIT_TOUCH_ONLY,    ///< Never; touch detection alone
} pah831x_operate_mode;

/**
 * PPG register profiles. Only profiles 1 to 3 hold settings:
 * 1 = 20 Hz, one channel from both green LEDs (heart rate);
 * 2 = 25 Hz, four channels: two red, two infrared (SpO2);
 * 3 = 25 Hz, one green channel (HRV).
 */
typedef enum {
    PAH831X_PPG_REG_SETTING_IDLE = 0,
    PAH831X_PPG_REG_SETTING_1,
    PAH831X_PPG_REG_SETTING_2,
    PAH831X_PPG_REG_SETTING_3,
    PAH831X_PPG_REG_SETTING_4,
    PAH831X_PPG_REG_SETTING_5,
    PAH831X_PPG_REG_SETTING_6,
    PAH831X_PPG_REG_SETTING_7,
    PAH831X_PPG_REG_SETTING_8,
    PAH831X_PPG_REG_SETTING_9,
    PAH831X_PPG_REG_SETTING_10,
} pah831x_ppg_reg_setting_table;

typedef enum {
    PAH831X_ECG_REG_SETTING_IDLE = 0,
    PAH831X_ECG_REG_SETTING_1,
    PAH831X_ECG_REG_SETTING_2,
    PAH831X_ECG_REG_SETTING_3,
    PAH831X_ECG_REG_SETTING_4,
    PAH831X_ECG_REG_SETTING_5,
    PAH831X_ECG_REG_SETTING_6,
    PAH831X_ECG_REG_SETTING_7,
    PAH831X_ECG_REG_SETTING_8,
    PAH831X_ECG_REG_SETTING_9,
    PAH831X_ECG_REG_SETTING_10,
} pah831x_ecg_reg_setting_table;

typedef enum {
    pah_AFE_GAIN_CHA_IDLE     = 0,
    pah_AFE_GAIN_CHA_75K      = 1,
    pah_AFE_GAIN_CHA_150K     = 2,
    pah_AFE_GAIN_CHA_350K     = 3,
    pah_AFE_GAIN_CHA_1000K    = 4,
    pah_AFE_GAIN_CHA_350K_LOW = 5,
} pah_afe_gain_e;

typedef enum {
    alg_status_idle,
    alg_status_process,
    alg_status_close,
    alg_status_finish,
    alg_status_data_weak,
} alg_status_e;

typedef enum {
    pah_alg_mode_IDLE    = 0x00,
    pah_alg_mode_HR_25HZ = 0x02,
    pah_alg_mode_HR      = 0x04,
    pah_alg_mode_SPO2    = 0x08,
    pah_alg_mode_HRV     = 0x10,
    pah_alg_mode_STRESS  = 0x20,
    pah_alg_mode_RR      = 0x40,
    pah_alg_mode_SS      = 0x80,
} pah_alg_mode_e;

typedef enum {
    pah_alg_mode_bit_HR_25HZ = 1,
    pah_alg_mode_bit_HR      = 2,
    pah_alg_mode_bit_SPO2    = 3,
    pah_alg_mode_bit_HRV     = 4,
    pah_alg_mode_bit_STRESS  = 5,
    pah_alg_mode_bit_RR      = 6,
    pah_alg_mode_bit_SS      = 7,
} pah_alg_mode_bit_e;

typedef enum {
    LOG_DATA_DISABLE = 0,
    LOG_DATA_ENABLE  = 1,
} lod_data_en_e;

typedef enum {
    HRD_ACTION_MODE_IDLE               = 0,
    HRD_ACTION_MODE_NORMAL             = 1,
    HRD_ACTION_MODE_OUTDOOR_RUN        = 2,
    HRD_ACTION_MODE_INDOOR_RUN         = 3,
    HRD_ACTION_MODE_OUTDOOR_WALK       = 4,
    HRD_ACTION_MODE_INDOOR_WALK        = 5,
    HRD_ACTION_MODE_OUTDOOR_BIKE       = 6,
    HRD_ACTION_MODE_INDOOR_BIKE        = 7,
    HRD_ACTION_MODE_Elliptical_trainer = 8,
    HRD_ACTION_MODE_Rowing_machine     = 9,
    HRD_ACTION_MODE_HITT               = 10,
    HRD_ACTION_MODE_BALL               = 11,
    HRD_ACTION_MODE_Swimming           = 12,
    HRD_ACTION_MODE_reserved1          = 13,
    HRD_ACTION_MODE_reserved2          = 14,
} hrd_action_mode_e;

/// Call from the data-ready interrupt (or its deferred handler)
void pah_interrupt_handle(void);

/// Verify the chip and keep plat. Returns the product ID, or 0 on failure.
uint16_t pah831x_sensor_init(pah_plat *plat);

/**
 * @param work_mode             pah831x_work_mode
 * @param operate_mode          pah831x_operate_mode
 * @param ppg_reg_setting       pah831x_ppg_reg_setting_table
 * @param ecg_reg_setting       pah831x_ecg_reg_setting_table
 * @param interrupt_report_num  Samples per channel in each batch (FIFO watermark)
 * @param sample_period         Sample period handed on to the algorithm, ms
 */
void pah831x_sensor_start(uint8_t work_mode, uint8_t operate_mode,
                          uint8_t ppg_reg_setting, uint8_t ecg_reg_setting,
                          uint16_t interrupt_report_num, float sample_period);

/// Read whatever the last interrupt flagged; call after pah_interrupt_handle()
void pah831x_sensor_task(void);

uint8_t      get_ppg_enable(void);
uint8_t      get_ppg_data_ready(void);
void         clear_ppg_data_ready(void);
ppg_data_out get_ppg_data_out(void);

uint8_t      get_ecg_enable(void);
uint8_t      get_ecg_data_ready(void);
void         clear_ecg_data_ready(void);
ecg_data_out get_ecg_data_out(void);

void     pah_sensor_stop(void);
void     set_ir_r_led_switch_status(bool status);
uint16_t get_pah831x_touch_value(void);

/// Read the touch value into *value; returns 0 if the bus read failed
uint8_t  get_pah831x_touch_value_checked(uint16_t *value);

/// 1 while the sensor detects skin contact, 0 otherwise
uint8_t  get_pah831x_touch(void);

#ifdef __cplusplus
}
#endif

#endif /* PAH831X_MAIN_H */
