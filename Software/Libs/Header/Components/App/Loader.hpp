/**
 ******************************************************************************
 * @file    AppLoader.hpp
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

#ifndef __APP_LOADER_HPP
#define __APP_LOADER_HPP

#include "SDK/Interfaces/IKernel.hpp"

#include "Interfaces/Components/IFileSystem.hpp"
#include "AppTypes.hpp"
#include "App/AppHeaders.hpp"
#include "Shared/AppLaunchFailure.hpp"

#include "cmsis_os.h"
#include "FreeRTOS.h"


namespace App
{

/**
 * @brief   Class for loading and unloading applications into memory.
 *
 * Manages application loading, memory allocation, relocation, and unloading
 * for dynamic firmware loading in embedded systems.
 */
class Loader {

public:

    /**
     * @brief Constructor.
     * @param appName: Application name.
     */
    Loader(const char* appName) : mAppName(appName)
    {}

    /**
     * @brief Default destructor.
     */
    virtual ~Loader();

    /**
     * @brief   Load MainHeader only.
     * @param   file: Binary file containing the application.
     * @return  True if the main header loaded successfully,
     *          otherwise false.
     */
    static bool ReadMainHeader(Interface::IFile& file, App::MainHeader& header);

    /**
     * @brief   Load an application from a file path into memory.
     * @param   file: Binary file containing the application.
     * @param   ikernel: Kernel interface object.
     * @return  True if the application was loaded successfully,
     *          otherwise false.
     */
    bool load(Interface::IFile&         file,
              SDK::Interface::IKernel&  ikernel,
              uint32_t                  offset = 0);

    /**
     * @brief   Unload the currently loaded application and free resources.
     * @param   mem: Reference to the memory pointer to be freed.
     * @param   thread: Thread handle of the loaded application to terminate.
     */
    void unload();

    /**
     * @brief   Check if application is loaded into memory.
     * @return  True if the application is loaded,
     *          otherwise false.
     */
    bool isLoaded() const;

    /**
     * @brief   Why the last load() failed.
     * @return  The reason, or LaunchFailure::NONE if the last load succeeded
     *          or none has been attempted. Only meaningful after load()
     *          returns false.
     */
    LaunchFailure getFailure() const { return mFailure; }

private:

    /// Application name
    const char* mAppName;

    /// Why the last load() failed, for the message the launcher shows.
    /// Nearly every bail-out in load() is the image being unreadable or
    /// invalid, so load() sets BAD_IMAGE on entry and the specific cases
    /// overwrite it. That way a new early return is categorised correctly by
    /// default rather than silently reporting the previous attempt's reason.
    LaunchFailure mFailure = LaunchFailure::NONE;

    /// The base address of the program for which it was compiled
    static constexpr uint32_t skCompileBaseAddress = 0x20000000;

    /*
     * @brief   Section pointers for quick access to different segments in RAM.
     */
    struct SectionPointers {
        uint32_t *text;
        uint32_t *preinit_array;
        uint32_t *init_array;
        uint32_t *fini_array;
        uint32_t *plt;
        uint32_t *data;
        uint32_t *got;
        uint32_t *bss;
        uint32_t *stack;
    };
    /// Section pointers for quick access to different segments in RAM.
    SectionPointers mSections { };

    /// A pointer that will store the allocated memory address
    uint32_t *mAppRam = nullptr;

    /// Thread handle where the application will run
    osThreadId_t mHandle = nullptr;

    /// Static task control block for managing the thread
    StaticTask_t mControlBlock { };

    /**
     * @brief   Validate the header of the binary file.
     * @param   header: Reference to the application header to validate.
     * @return  True if the header is valid, otherwise false.
     */
    bool validateHeader(const App::UappHeader& header);

    /**
     * @brief   Read a section of the binary file into a buffer.
     * @param   file: Binary file containing the application.
     * @param   offset: Offset from the beginning of the file in words.
     * @param   buffer: Pointer to the buffer where data will be stored.
     * @param   length: Length of the section to read in words.
     * @return  True if the section was read successfully, otherwise false.
     */
    bool readSection(Interface::IFile &file, uint32_t offset, uint32_t *buffer,
            uint32_t length);

    /**
     * @brief   Load sections from a binary file into RAM.
     * @param   file: Binary file containing the application.
     * @param   header: Reference to the UAPP header containing metadata
     *          about sections.
     * @param   pRAM: Pointer to the starting address in RAM where sections
     *          will be loaded.
     * @return  True if all sections were successfully loaded, otherwise false.
     */
    bool loadSections(Interface::IFile &file, const UappHeader &header,
            uint32_t *pRAM, uint32_t offset);

    /// Relocation entries read from the file at a time (see relocate()).
    static constexpr uint32_t skRelocChunkWords = 64;

    /**
     * @brief   Apply relocations to the loaded sections.
     * @details The relocation table is read straight from the file, a chunk
     *          at a time, so a load needs no buffer the size of the table.
     * @param   file: Binary file containing the application.
     * @param   header: Reference to the UAPP header containing relocation
     *          information.
     * @param   sections: Reference to a structure with pointers to relevant
     *          sections in RAM.
     * @param   relOffsetW: Offset of the relocation table in the file, in
     *          words.
     * @param   offset: Offset to be applied to each relocation entry.
     * @return  True if every entry was read and applied, false if the file
     *          could not be read (the image is then partly relocated).
     */
    bool relocate(Interface::IFile &file, const App::UappHeader &header,
            SectionPointers &sections, uint32_t relOffsetW, uint32_t offset);

    /**
     * @brief   Apply an offset to an array of addresses in RAM.
     * @param   pBaseddr: Pointer to the base address of the array in RAM.
     * @param   arraySize: Size of the array in words.
     * @param   offset: Offset to be applied to each element of the array.
     */
    void relocateArray(uint32_t *pBaseddr, uint32_t arraySize,
            uint32_t offset);

    /**
     * @brief   Run the loaded application in a new thread.
     * @param   header: Reference to the UAPP header containing metadata
     *          about the application.
     * @param   sections: Reference to a structure with pointers to relevant
     *          sections in RAM.
     * @return  True if the application was successfully started,
     *          otherwise false.
     */
    bool runAPP(const App::UappHeader &header, const SectionPointers &sections);

    /**
     * @brief   Log the memory layout of the loaded application.
     * @param   header: Rage name for printing.
     * @param   pMemddr: Base address of the memory range.
     * @param   size:  Renge size in words.
     */
    static void LogMemoryLayout(const char *sectionName, uint32_t *pBaseddr,
            uint32_t size);

};

} // namespace App

#endif // __APP_LOADER_HPP
