/**
 ******************************************************************************
 * @file    AppLoader.сpp
 * @brief   Dynamic application loading into memory.
 * @details The idea and part of the implementation for dynamic loading
 *          are borrowed from the following project:
 *          https://github.com/rgujju/Dynamic_App_Loading/tree/freertos
 *
 * @copyright
 *          Contains code adapted from Dynamic_App_Loading, under its MIT
 *          License:
 *
 *          Copyright (c) 2020 Rohit Gujarathi
 *
 *          Permission is hereby granted, free of charge, to any person
 *          obtaining a copy of this software and associated documentation
 *          files (the "Software"), to deal in the Software without
 *          restriction, including without limitation the rights to use,
 *          copy, modify, merge, publish, distribute, sublicense, and/or sell
 *          copies of the Software, and to permit persons to whom the Software
 *          is furnished to do so, subject to the following conditions:
 *
 *          The above copyright notice and this permission notice shall be
 *          included in all copies or substantial portions of the Software.
 *
 *          THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *          EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *          MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *          NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *          BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *          ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *          CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *          SOFTWARE.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "App/Loader.hpp"
#include "App/ImageHeap.hpp"

#include <cassert>
#include <malloc.h>  // malloc_trim, called when an app image is released

#define LOG_MODULE_PRX      "Loader"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace App
{

Loader::~Loader()
{
    unload();
}

bool Loader::ReadMainHeader(Interface::IFile& file, App::MainHeader& header)
{
    if (!file.open()) {
        LOG_ERROR("Failed to open the file, [%s]\n", file.getPath());
        return false;
    }

    size_t br;
    bool res = file.read((char*)&header, sizeof(header), br);

    file.close();

    if (!res) {
        LOG_ERROR("Failed to read the file, [%s]\n", file.getPath());
    }

    return res;
}

bool Loader::load(Interface::IFile&          file,
                  SDK::Interface::IKernel&   ikernel,
                  uint32_t                   offset)
{
    // Assume the image is at fault. Almost every bail-out below is a file or
    // header problem, and the allocation sites overwrite this - so a return
    // added later is categorised sensibly by default instead of reporting
    // whatever the previous attempt failed with.
    mFailure = LaunchFailure::BAD_IMAGE;

    if (!file.open()) {
        LOG_ERROR("Failed to open file [%s]\n", file.getPath());
        return false;
    }

    if (!file.seek(offset)) {
        LOG_ERROR("Failed to seek to the firmware\n");
        file.close();
        return false;
    }

    // Read the header
    App::UappHeader header {};
    size_t br;
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header), br) || br != sizeof(header)) {
        LOG_ERROR("Failed to read header\n");
        file.close();
        return false;
    }

    if (!validateHeader(header)) {
        file.close();
        return false;
    }

    // Calculate the memory size and allocate it.
    //
    // Accumulated in 64 bits because every term is attacker-controlled: these
    // are header fields from a file that is writable over USB, and summing
    // them in uint32_t wraps silently. text_sz = UINT32_MAX with data_sz = 2
    // and stack_sz = 1 sums to 2, so checking the TOTAL afterwards is too
    // late - the total is already a small, plausible number. The sum itself
    // has to be exact, which is what the wider accumulator buys.
    const uint64_t memSizeW64 =
            static_cast<uint64_t>(header.text_sz)          + header.text_pads +
            static_cast<uint64_t>(header.preinit_array_sz) + header.preinit_array_pads +
            static_cast<uint64_t>(header.init_array_sz)    + header.init_array_pads +
            static_cast<uint64_t>(header.fini_array_sz)    + header.fini_array_pads +
            static_cast<uint64_t>(header.plt_sz)           + header.plt_pads +
            static_cast<uint64_t>(header.data_sz)          + header.data_pads +
            static_cast<uint64_t>(header.got_sz)           + header.got_pads +
            static_cast<uint64_t>(header.bss_sz)           + header.bss_pads +
            static_cast<uint64_t>(header.stack_sz);

    // One bound covers everything downstream: it keeps the byte conversion
    // below from wrapping, and keeps the value inside uint32_t so the section
    // pointer arithmetic stays exact. `new (std::nothrow) uint32_t[memSizeW]`
    // used to reject the byte conversion for us - the standard requires the
    // nothrow array form to return null when the size computation overflows -
    // and allocating by byte count carries no such guarantee.
    //
    // A refusal here costs an app launch. The alternative costs an eight-byte
    // reservation that loadSections writes an arbitrary amount past.
    static constexpr uint64_t skMaxImageWords =
            (SIZE_MAX / sizeof(uint32_t) < UINT32_MAX)
                    ? (SIZE_MAX / sizeof(uint32_t))
                    : UINT32_MAX;

    if (memSizeW64 > skMaxImageWords) {
        // The individual fields rather than the total: the total is only
        // large because one of these is absurd, and naming them says which.
        // %u only, so this does not depend on long-long printf support.
        LOG_ERROR("App [%s] header sizes are not loadable: text %u, data %u, "
                "bss %u, got %u, stack %u words\n", mAppName,
                static_cast<unsigned>(header.text_sz),
                static_cast<unsigned>(header.data_sz),
                static_cast<unsigned>(header.bss_sz),
                static_cast<unsigned>(header.got_sz),
                static_cast<unsigned>(header.stack_sz));
        file.close();
        return false;
    }

    const uint32_t memSizeW = static_cast<uint32_t>(memSizeW64);

    // loadSections re-sums a SUBSET of the terms above (text..got, without
    // bss and stack) in uint32_t. That cannot wrap now: every term is
    // non-negative, so the subset is bounded by the total just checked.

    LOG_INFO("App [%s] required RAM: %u bytes\n", mAppName, memSizeW * sizeof(uint32_t));
    // The image comes from the top-down region rather than the kernel arena.
    // It is the one allocation large enough that living inside the
    // arena is harmful in itself: anything the kernel allocates above it stays
    // fenced until the app unloads, and the hole it leaves on unload is in the
    // middle of the arena, where a trim cannot return it.
    uint32_t *mem = static_cast<uint32_t*>(
            ImageHeap::allocate(memSizeW * sizeof(uint32_t)));
    LOG_DEBUG("mem 0x%08X\n", mem);
    if (mem == nullptr) {
        LOG_ERROR("Failed to allocate %u bytes for the app\n", memSizeW * sizeof(uint32_t));
        mFailure = LaunchFailure::OUT_OF_MEMORY;
        file.close();
        return false;
    }

    // The relocation table is read from the file a chunk at a time by
    // relocate(), once the sections are in place. It used to be read whole
    // into a kernel-heap buffer first - 21.6 KB for Run - and that buffer had
    // to come from the break, right under the image just placed. On a watch
    // whose resident apps leave Run's two images a gap with only a few
    // kilobytes over, the image fitted and the table did not.
    const uint32_t relOffsetW = (offset / 4) + (sizeof(header) / 4);

    // Copy sections to the RAM
    if (!loadSections(file, header, mem, offset)) {
        ImageHeap::release(mem);
        file.close();
        return false;
    }

    // Pointers to the sections in RAM
    mSections.text          = mem;
    mSections.preinit_array = mSections.text          + header.text_sz +        + header.text_pads;
    mSections.init_array    = mSections.preinit_array + header.preinit_array_sz + header.preinit_array_pads;
    mSections.fini_array    = mSections.init_array    + header.init_array_sz    + header.init_array_pads;
    mSections.plt           = mSections.fini_array    + header.fini_array_sz    + header.fini_array_pads;
    mSections.data          = mSections.plt           + header.plt_sz           + header.plt_pads;
    mSections.got           = mSections.data          + header.data_sz          + header.data_pads;
    mSections.bss           = mSections.got           + header.got_sz           + header.got_pads;
    mSections.stack         = mSections.bss           + header.bss_sz;

    LOG_DEBUG_WP("\n");
    LogMemoryLayout("Memory range",   mem,                     memSizeW);
    LogMemoryLayout(".text",          mSections.text,          header.text_sz);
    LogMemoryLayout(".preinit_array", mSections.preinit_array, header.preinit_array_sz);
    LogMemoryLayout(".init_array",    mSections.init_array,    header.init_array_sz);
    LogMemoryLayout(".fini_array",    mSections.fini_array,    header.fini_array_sz);
    LogMemoryLayout(".plt",           mSections.plt,           header.plt_sz);
    LogMemoryLayout(".data",          mSections.data,          header.data_sz);
    LogMemoryLayout(".got",           mSections.got,           header.got_sz);
    LogMemoryLayout(".bss",           mSections.bss,           header.bss_sz);
    LogMemoryLayout(".stack",         mSections.stack,         header.stack_sz);

    // Replace the IKernel address to the one on the MCU
    // It must be located at the beginning of the data section
    *mSections.data = reinterpret_cast<uintptr_t>(&ikernel);

    LOG_DEBUG("Address IKernel 0x%08X = 0x%08X\n",
            reinterpret_cast<uint32_t>(mSections.data),
            reinterpret_cast<uint32_t>(*mSections.data));

    // Make relocation
    uint32_t relocateOffset = reinterpret_cast<uint32_t>(mem) - skCompileBaseAddress;

    LOG_DEBUG("Relocate Offset 0x%08X\n", relocateOffset);

    if (!relocate(file, header, mSections, relOffsetW, relocateOffset)) {
        // Partly relocated, so not runnable - but nothing has run it yet, and
        // the region takes it back whole.
        ImageHeap::release(mem);
        file.close();
        return false;
    }

    // The file is no longer needed
    file.close();

    // Run APP
    if (!runAPP(header, mSections)) {
        ImageHeap::release(mem);
        return false;
    }

    mAppRam  = mem;

    return true;
}

bool Loader::runAPP(const App::UappHeader &header, const SectionPointers &sections)
{
    const TaskFunction_t entry = reinterpret_cast<TaskFunction_t>(
                // OR'ed with 1 to set the thumb bit
                reinterpret_cast<uintptr_t>(sections.text) | 1
            );

    LOG_DEBUG("Entry 0x%08X\n", reinterpret_cast<uint32_t>(entry));

    const osThreadAttr_t kAttr = {
            .name       = mAppName,
            .cb_mem     = &mControlBlock,
            .cb_size    = sizeof(mControlBlock),
            .stack_mem  = sections.stack,
            .stack_size = header.stack_sz * 4 - 4,
            .priority   = osPriorityNormal
    };

    // Pass GOT table address to r9 as argument to the function
    mHandle = osThreadNew(entry, sections.got, &kAttr);

    LOG_INFO("Task: [%s] started. Stack size: %u bytes, Address 0x%08X\n", mAppName,
                                                                           header.stack_sz * 4,
                                                                           sections.stack);

    if (mHandle == nullptr) {
        // NOT a memory failure. kAttr supplies both cb_mem and stack_mem, so
        // cmsis_os2.c takes the xTaskCreateStatic branch and never asks the
        // FreeRTOS heap for anything. A null handle here means the attributes
        // were rejected - an invalid priority, or a stack_size derived from
        // header.stack_sz that the kernel will not accept - so the image is
        // what is wrong, and telling the user to free memory would send them
        // after space they already have.
        mFailure = LaunchFailure::BAD_IMAGE;
        LOG_ERROR("Failed to create thread for the application\n");
        return false;
    }

    mFailure = LaunchFailure::NONE;
    return true;
}

void Loader::unload()
{
    // The reason described a load that no longer exists. Every caller now
    // sets its own before reporting, so nothing reads this - but leaving a
    // stale reason behind is how the bug above happened in the first place.
    mFailure = LaunchFailure::NONE;

    if (mHandle) {
        osThreadTerminate(mHandle);
        mHandle = nullptr;
    }

    if (mAppRam) {
        ImageHeap::release(mAppRam);
        mAppRam = nullptr;

        // Still worth trimming, for a better reason than before. The image is
        // no longer a kernel chunk, so this is not about returning it; it is
        // about the break. An app that ran will have grown the arena, and under
        // the two-ended heap the break is exactly what image placement is clamped against, so
        // lowering it here is what hands the space back to the NEXT image.
        (void)malloc_trim(0);
    }
}

bool Loader::isLoaded() const
{
    return mHandle || mAppRam;
}

bool Loader::validateHeader(const App::UappHeader &header)
{
    if (strncmp(reinterpret_cast<const char*>(header.magic), "UAPP", 4) != 0) {
        LOG_ERROR("Invalid UAPP file\n");
        return false;
    }

    if (header.text_sz == 0 || header.data_sz == 0) {
        LOG_ERROR("Invalid section sizes\n");
        return false;
    }

    return true;
}

bool Loader::readSection(Interface::IFile &file, uint32_t offset,
        uint32_t *pBuffer, uint32_t length)
{
    size_t br = 0;

    if (!file.seek(offset * 4)
            || !file.read(reinterpret_cast<char*>(pBuffer), (length * 4), br)
            || br != (length * 4))
    {
        LOG_ERROR("Failed to read section at offset %u, length %u\n", offset, length);
        return false;
    }

    return true;
}

bool Loader::loadSections(Interface::IFile &file, const App::UappHeader &header,
        uint32_t *pRAM, uint32_t offset)
{
    // Copy app to the RAM
    uint32_t toCopyW = (header.text_sz          + header.text_pads +
                        header.preinit_array_sz + header.preinit_array_pads +
                        header.init_array_sz    + header.init_array_pads +
                        header.fini_array_sz    + header.fini_array_pads +
                        header.plt_sz           + header.plt_pads +
                        header.data_sz          + header.data_pads +
                        header.got_sz           + header.got_pads);

    uint32_t offsetW = (offset / 4)         +
                       (sizeof(header) / 4) +
                       header.rel_addr_list_sz;

    return readSection(file, offsetW, pRAM, toCopyW);
}

bool Loader::relocate(Interface::IFile      &file,
                      const App::UappHeader &header,
                      SectionPointers       &section,
                      uint32_t              relOffsetW,
                      uint32_t              offset)
{

    // Relocate absolute addresses
    if (header.rel_addr_list_sz > 0) {
        LOG_DEBUG_WP("\n");
        LOG_DEBUG("Relocate absolute addresses:\n");

        // On the stack rather than shared: each app loads on its own thread,
        // and a dozen glances load at once. Small, because this runs on the
        // app thread's 4 KB stack with FatFs's own frames below it.
        uint32_t chunk[skRelocChunkWords];

        for (uint32_t done = 0; done < header.rel_addr_list_sz; ) {
            const uint32_t left  = header.rel_addr_list_sz - done;
            const uint32_t count = (left < skRelocChunkWords) ? left : skRelocChunkWords;

            if (!readSection(file, relOffsetW + done, chunk, count)) {
                return false;
            }

            for (uint32_t i = 0; i < count; i++) {
                uint32_t address_bin = chunk[i];
                uint32_t address_ram = address_bin + offset;

                uint32_t oldValue = *(reinterpret_cast<uint32_t*>(address_ram));
                // Preserve unused or uninitialized fields
                uint32_t newValue = (oldValue == 0) ? 0 : oldValue + offset;

                *(reinterpret_cast<uint32_t*>(address_ram)) = newValue;

                LOG_DEBUG("  0x%08X:0x%08X -> 0x%08X:0x%08X\n", address_bin,
                                                                oldValue,
                                                                address_ram,
                                                                newValue);
            }

            done += count;
        }
    }

    // .preinit_array
    if (header.preinit_array_sz > 0) {
        LOG_DEBUG_WP("\n");
        LOG_DEBUG("Relocate .preinit_array:\n");
        relocateArray(section.preinit_array, header.preinit_array_sz, offset);
    }

    // .init_array
    if (header.init_array_sz > 0) {
        LOG_DEBUG_WP("\n");
        LOG_DEBUG("Relocate .init_array:\n");
        relocateArray(section.init_array, header.init_array_sz, offset);
    }

    // .fini_array
    if (header.fini_array_sz > 0) {
        LOG_DEBUG_WP("\n");
        LOG_DEBUG("Relocate .fini_array:\n");
        relocateArray(section.fini_array, header.fini_array_sz, offset);
    }

    // .plt
    if (header.plt_sz > 0) {
        LOG_DEBUG_WP("\n");
        LOG_DEBUG("Relocate .plt:\n");
        relocateArray(section.plt, header.plt_sz, offset);
    }

    // .got
    if (header.got_sz > 0) {
        LOG_DEBUG_WP("\n");
        LOG_DEBUG("Relocate .got:\n");
        relocateArray(section.got, header.got_sz, offset);
    }

    LOG_DEBUG_WP("\n");
    return true;
}

void Loader::relocateArray(uint32_t *pBaseddr, uint32_t arraySize,
        uint32_t offset)
{
    for (uint32_t i = 0; i < arraySize; i++) {
        uint32_t oldValue = pBaseddr[i];

        if (oldValue >= 0x08028000 && oldValue < 0x08060000) {
            LOG_INFO("#%u: skipped address : 0x%08X\n", i, oldValue);
            continue;
        }

        // Preserve unused or uninitialized fields
        uint32_t newValue = (oldValue == 0) ? 0 : oldValue + offset;

        pBaseddr[i] = newValue;
        LOG_DEBUG("#%u: 0x%08X -> 0x%08X\n", i, oldValue, newValue);
    }
}

void Loader::LogMemoryLayout(const char *sectionName,
                                    uint32_t   *pBaseddr,
                                    uint32_t    size)
{
    LOG_DEBUG("%-15s [0x%08X - 0x%08X] : %6ld\n", sectionName,
                                                  reinterpret_cast<uint32_t>(pBaseddr),
                                                  reinterpret_cast<uint32_t>(pBaseddr + size),
                                                  size);
}

} // namespace App
