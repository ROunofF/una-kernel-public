# Vendor library

`lib/libuna_vendor.a` holds vendor code that the firmware needs but whose source code is not public. UNA compiles it into this library, which the firmware links like any other. The headers in `include/` declare everything the library provides.

| Part | Headers | What it provides |
| --- | --- | --- |
| PixArt PAH831x driver | `include/pixart/pah831x_main.h` | Starts and stops the PAH8316 optical heart-rate sensor and delivers batches of PPG samples |
| PixArt factory tests | `include/pixart/pah831x_factory_test.h` | Production-line checks of the sensor's LEDs, light path and interrupt lines |
| PixArt HRD v5653 | `include/pixart/alg/pah8series_api_c.h` | Heart rate, with motion compensation from the accelerometer |
| PixArt HRV V3131 | `include/pixart/alg/pxialg_hrv_v3.h` | Beat-to-beat (R-R) intervals, SDNN, RMSSD, LF/HF ratio, respiration rate |
| PixArt SHRD V307009 | `include/pixart/alg/pxialg.h` | Heart rate, as an input to SpO2 |
| PixArt SpO2 V587 | `include/pixart/alg/pxialg_spo2.h` | Blood oxygen saturation; needs the heart rate (`SpO2_SetHrValue()`), which PixArt's example code takes from SHRD |
| ST BlueNRG-2 host library | `include/ble/una_ble.h` | Commands to and events from the watch's BlueNRG-2 BLE coprocessor |

The headers in `include/pixart/alg/` are PixArt's. All the others are UNA's.

## Linking

The library is built for the Cortex-M33 with the hard-float ABI (`-mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard`). Add `include/pixart/`, `include/pixart/alg/` and `include/ble/` to the include path, and link with `-L lib -luna_vendor`.

The firmware must provide:

- `pah_port_log()`, declared in `include/pixart/pah_port.h`, which writes the PixArt driver's log messages.
- Every `una_ble_..._event()` function declared in `include/ble/una_ble.h`, which receive the BLE events.
- `HAL_GetTick()` from the STM32 HAL, which times out BLE commands.
- From the C library: `memcpy`, `memmove`, `memset`, `qsort`, `printf`, `puts`, `cosf`, `sinf`, `tanf`, `expf`, `logf`, `log10f` and `sqrtf`, and the `__aeabi_*` double-precision helpers.

## PixArt PAH8316

The algorithms keep their working state in memory that the caller provides: each has a function that reports how much it needs, such as `pah8series_query_open_size()`. The PixArt code makes no other allocations.

### Using the sensor

Fill in a `pah_plat` with the I2C, delay and tick functions, and pass it to `pah831x_sensor_init()`. Then start the sensor with `pah831x_sensor_start()` and one of these PPG register profiles:

| Profile | Rate | Channels | For |
| --- | --- | --- | --- |
| `PAH831X_PPG_REG_SETTING_1` | 20 Hz | 1: both green LEDs | HRD |
| `PAH831X_PPG_REG_SETTING_2` | 25 Hz | 4: two red, two infrared | SpO2 and SHRD |
| `PAH831X_PPG_REG_SETTING_3` | 25 Hz | 1: one green LED | HRV |

On each data-ready interrupt, call `pah_interrupt_handle()` and then `pah831x_sensor_task()`. When `get_ppg_data_ready()` is set, `get_ppg_data_out()` returns the batch; pass it to the algorithm, then call `clear_ppg_data_ready()`.

### Factory tests

Fill in a `pah831x_factory_test_plat` with the I2C and delay functions, and a `read_int` function that reads the sensor's two interrupt lines. Power the sensor up, then call `pah831x_factory_test_run()` for each test. It logs each measurement against its limits and returns whether all of them passed. `INT_TEST` needs both interrupt lines set up as inputs with pull-ups.

The firmware's use of the PixArt code, including the session code that runs the heart-rate algorithm, is in `Software/Libs/Source/Hardware/PAH8316LS/`.

## ST BlueNRG-2

`una_ble.h` is UNA's interface to ST's host library for the BlueNRG-2. Each command function sends the ACI or HCI command of the same name in ST's BlueNRG-1/2 ACI command reference, so `una_ble_gap_terminate()` sends `aci_gap_terminate`, and `una_ble_hci_le_rand()` sends `hci_le_rand`. Each `una_ble_..._event()` function receives the event of the same name. Events without a `una_ble_` function go to ST's default handler, which only prints the event's name.

Fill in a `una_ble_hci_io` with the functions that reach the coprocessor over SPI. Call `una_ble_hci_init()`, then `una_ble_hci_register_io_bus()`. While the coprocessor's IRQ line is high, call `una_ble_hci_notify_asynch_evt()` to read each packet, and call `una_ble_hci_user_evt_proc()` from a thread to deliver the events.

The firmware's use of the BLE interface is in `Software/Libs/Source/Components/External/BLE/`.

## Licence

Each vendor's code in the library is under that vendor's terms.

- **PixArt:** binary only, with the headers needed to use it. The library contains code copyright PixArt Imaging Inc., distributed as a compiled library with PixArt's permission.
- **ST:** the BlueNRG-2 host library is under ST's licence SLA0055. It is licensed for use on a product combining an ST microcontroller with an ST device, which the watch's STM32U5 and BlueNRG-2 are.
