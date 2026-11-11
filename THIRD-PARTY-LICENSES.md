# Third-Party Licenses

The MIT license in [`LICENSE`](LICENSE) covers **UNA Watch Ltd's own source code** in this repository.

The third-party components below are **licensed separately under their own terms and are NOT covered by this repository's MIT license**. So is any file whose header names another copyright holder or licence. Each component keeps its own copyright notices and licence; consult those before redistributing.

| Component | Location | Licence |
|-----------|----------|---------|
| Vendor library: PixArt PAH8316 driver, factory tests and HRD, HRV, SHRD and SpO2 algorithms | `Software/ThirdParty/VendorLib/lib/libuna_vendor.a`, `Software/ThirdParty/VendorLib/include/pixart/alg/` | Binary only, with the headers needed to use it. Contains code copyright PixArt Imaging Inc., distributed as a compiled library with PixArt's permission. |
| Vendor library: ST BlueNRG-2 host library | `Software/ThirdParty/VendorLib/lib/libuna_vendor.a` | STMicroelectronics **SLA0055**. Binary only. Licensed for use on a product that combines an STMicroelectronics microcontroller with an STMicroelectronics device, and must not be made subject to any open-source licence terms. |
| TouchGFX | `Software/App/TouchGFX-GUI/touchgfx/` | STMicroelectronics **SLA0048**; see [`LICENSE.txt`](Software/App/TouchGFX-GUI/touchgfx/LICENSE.txt) there. Use and redistribution are permitted only with a microcontroller or microprocessor made by or for STMicroelectronics, and it must not be made subject to any open-source licence terms. TouchGFX bundles further third-party code under its own licences, including a rasterizer based on Anti-Grain Geometry, libjpeg (Independent JPEG Group), and the SDL2, SDL2_image, libpng and zlib libraries used by the PC simulator (zlib and libpng licences). |
| STM32Cube USB composite device (AL94) | `Software/ThirdParty/ST/AL94_USB_Composite/`, `Software/Libs/Source/Components/USB/usbd_composite.c`, `Software/Libs/Source/Components/USB/usbd_desc.c` | STMicroelectronics **SLA0044** (see each file's header). Use is permitted only with an STMicroelectronics microcontroller or microprocessor, and it must not be made subject to any open-source licence terms. |
| STM32U5 HAL and device files | `Software/ThirdParty/ST/Drivers/` | BSD-3-Clause (HAL) and Apache-2.0 (CMSIS); see the `LICENSE` files there. |
| STM32CubeMX-generated and ST template files | `Software/App/UnaWatch-CubeIDE/Core/`, the USB device files in `Software/Libs/Source/Components/USB/`, `una-libs/Software/App/*/Core/` | STMicroelectronics' terms, as stated in each file's header. |
| newlib heap glue (Dave Nadler) | `Software/App/UnaWatch-CubeIDE/Core/Src/heap_useNewlib_ST.cpp` | BSD-style licence in the file's header. |
| FreeRTOS | `Software/ThirdParty/FreeRTOS/`, `Software/ThirdParty/ST/FreeRTOS/` | MIT. The Percepio trace recorder in `FreeRTOS-Plus-Trace` is Apache-2.0, except SEGGER RTT in its `streamports/Jlink_RTT/`, which is under SEGGER's BSD-style licence in the file headers. The FreeRTOS distribution includes further files under their own licences, named in their headers. |
| FatFs | `Software/ThirdParty/FatFs/`; `delete_node()` in `Software/Libs/Source/Components/FileSystem/Folder.c` is adapted from its documentation | FatFs licence (BSD-style, one clause); see `LICENSE.txt`. |
| Embedded Template Library | `Software/ThirdParty/ETLCPP/` | MIT. |
| Bosch Sensortec BMI270 and BMM350 Sensor APIs | `Software/ThirdParty/BOSCH/` | BSD-3-Clause. |
| printf (Marco Paland) | `Software/ThirdParty/mpaland/` | MIT. |
| QR Code generator (Project Nayuki) | `Software/ThirdParty/Nayuki/` | MIT. |
| Dynamic app loading (Rohit Gujarathi) | Parts of `Software/Libs/Source/Components/App/Loader.cpp` and `Software/Libs/Header/Components/App/Loader.hpp` | MIT; the notice is in the files' headers. |
| MD5 (RSA Data Security, Inc.) | `Software/Libs/Source/Components/MD5/`, `Software/Libs/Header/Components/MD5/` | Derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm; RSA's notice is in the files' headers. |
| exFAT up-case table | `Software/Libs/Header/Components/FileSystem/exfat_helper.h` | Data defined by Microsoft's published exFAT file system specification. |
| AES in Python (Adam Newman) | `Software/Utilities/Scripts/firmware_packer/aespython/` | MIT. |
| Montserrat and Poppins fonts | `Software/App/TouchGFX-GUI/assets/fonts/` | SIL Open Font License 1.1; see [`OFL.txt`](Software/App/TouchGFX-GUI/assets/fonts/OFL.txt). |
| Shared C library image | `una-libs/` | Built from the GNU Arm toolchain's newlib (BSD-style licences), libstdc++ and libgcc (GPL with the GCC Runtime Library Exception). UNA's build project around it is MIT. |
| Bootloader image | `una-bootloader/Exe/` | UNA Watch Ltd. Binary only. It contains STMicroelectronics code under SLA0044 and the STM32 HAL. |

> Note: because SLA0044, SLA0048 and SLA0055 forbid subjecting those components to open-source terms, the
> repository's MIT license is scoped to UNA Watch Ltd's own code only and does not extend to them.
