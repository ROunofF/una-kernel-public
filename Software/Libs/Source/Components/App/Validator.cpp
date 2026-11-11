/**
 ******************************************************************************
 * @file    Validator.cpp
 * @brief   Validation logic for User Applications (headers, CRC, sections)
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "App/Validator.hpp"
#include "AppTypes.hpp"
#include "Crc/Crc32.hpp"


#define LOG_MODULE_PRX      "App.Validator"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include <string>
#include <cstring>

namespace App
{

/**
 * @brief Construct a validator with the expected LibC ABI version.
 *
 * Initializes the validator with the LibC version against which user-app
 * bundles will be validated (e.g., 0x00000002). No I/O is performed.
 *
 * @param libCVersion Expected LibC version used for compatibility checks.
 */
Validator::Validator(uint32_t libCVersion)
    : mLibCVersion(libCVersion)
{
}

/**
 * @brief Validate a user app file end-to-end, with safe open/close semantics.
 *
 * Tries to open the file if it is not already open, runs full validation
 * via @ref isValidA, and closes the file if it was originally closed.
 *
 * @param file File interface to the user app bundle.
 * @return true if the file passes validation; false otherwise.
 *
 * @post If the file was initially closed, it will be closed before return.
 */
bool Validator::appIsValid(Interface::IFile &file)
{
    const bool wasOpened = file.isOpen();

    if (!wasOpened) {
        if (!file.open()) {
            LOG_ERROR("failed to open the file\n");
            return false;
        }
    }

    bool status = isValid(file);

    if (wasOpened == false) {
        file.close();
    }

    return status;
}

/**
 * @brief Perform full validation assuming the file is accessible.
 *
 * Sequence:
 *  1. Basic size check to hold @ref MainHeader and trailing CRC.
 *  2. Read and validate the main header.
 *  3. Validate the global CRC over the full file payload.
 *  4. Sanity-check @ref MainHeader fields.
 *  5. Validate contained user-app sections (service and optional GUI).
 *
 * @param file File interface positioned at start (method will seek internally).
 * @return true on success, false on any validation error.
 */
bool Validator::isValid(Interface::IFile &file)
{
    if (file.size() < sizeof(App::MainHeader) + sizeof(uint32_t)) {
        LOG_ERROR("file size is invalid\n");
        return false;
    }

    if (!checkCRC(file, 0, file.size())) {
        LOG_ERROR("CRC is invalid\n");
        return false;
    }

    if (!file.seek(0)) {
        LOG_ERROR("failed to seek the file\n");
        return false;
    }

    App::MainHeader mainHeader;
    size_t br;
    if (!file.read((char*)&mainHeader, sizeof(App::MainHeader), br)) {
        LOG_ERROR("failed to load the main header\n");
        return false;
    }

    if (!checkMainHeader(mainHeader, file.size())) {
        LOG_ERROR("main header is invalid\n");
        return false;
    }

    if (!checkUserApps(file, mainHeader)) {
        return false;
    }

    return true;
}

/**
 * @brief Validate a variant-alias file end-to-end, with safe open/close semantics.
 *
 * Mirrors @ref appIsValid: opens the file if needed, runs @ref isValidAlias,
 * and restores the original open state.
 *
 * @param file File interface to the alias .uapp.
 * @return true if the file is a valid variant alias; false otherwise.
 */
bool Validator::aliasIsValid(Interface::IFile &file)
{
    const bool wasOpened = file.isOpen();

    if (!wasOpened) {
        if (!file.open()) {
            LOG_ERROR("failed to open the file\n");
            return false;
        }
    }

    bool status = isValidAlias(file);

    if (wasOpened == false) {
        file.close();
    }

    return status;
}

/**
 * @brief Perform full alias validation assuming the file is accessible.
 *
 * Sequence:
 *  1. Basic size check against the fixed alias layout.
 *  2. Validate the global CRC over the full file payload.
 *  3. Read the main header and the VariantAliasPayload.
 *  4. Sanity-check both via @ref checkAliasHeader.
 *
 * The embedded config is covered by the CRC but never parsed here.
 *
 * @param file File interface positioned at start (method will seek internally).
 * @return true on success, false on any validation error.
 */
bool Validator::isValidAlias(Interface::IFile &file)
{
    if (file.size() < App::kAliasFixedSize) {
        LOG_ERROR("alias file size is invalid\n");
        return false;
    }

    if (!checkCRC(file, 0, file.size())) {
        LOG_ERROR("CRC is invalid\n");
        return false;
    }

    if (!file.seek(0)) {
        LOG_ERROR("failed to seek the file\n");
        return false;
    }

    // FatFs reports success with a short count at EOF, so check br as well.
    App::MainHeader mainHeader;
    size_t br = 0;
    if (!file.read((char*)&mainHeader, sizeof(App::MainHeader), br) ||
            br != sizeof(App::MainHeader)) {
        LOG_ERROR("failed to load the main header\n");
        return false;
    }

    if (!file.seek(App::kAliasPayloadOffset)) {
        LOG_ERROR("failed to seek the file\n");
        return false;
    }

    App::VariantAliasPayload payload;
    if (!file.read((char*)&payload, sizeof(App::VariantAliasPayload), br) ||
            br != sizeof(App::VariantAliasPayload)) {
        LOG_ERROR("failed to load the alias payload\n");
        return false;
    }

    if (!checkAliasHeader(mainHeader, payload, file.size())) {
        LOG_ERROR("alias header is invalid\n");
        return false;
    }

    return true;
}

/**
 * @brief Check CRC32 of a region that includes a trailing stored CRC.
 *
 * The region \[offset, offset+size) is expected to end with a 4-byte CRC
 * value. The method:
 *  - bounds-checks the requested range,
 *  - reads the stored CRC from the last 4 bytes of the range,
 *  - recomputes CRC32 over the preceding bytes,
 *  - compares computed CRC with the stored value.
 *
 * @param file   File interface.
 * @param offset Start offset of the region.
 * @param size   Total region size IN BYTES including the trailing 4-byte CRC.
 * @return true if computed CRC matches the stored CRC; false otherwise.
 *
 * @note Uses @ref CRC32::calculate(buffer, len, seed) for incremental CRC.
 * @note Seeks the file to @p offset for the calculation pass.
 */
bool Validator::checkCRC(Interface::IFile &file, uint32_t offset, uint32_t size)
{
    if (size <= sizeof(uint32_t)) {
        LOG_ERROR("invalid file size\n");
        return false;
    }

    const auto n = file.size();
    if (offset > n || size > n - offset) {
        LOG_ERROR("out of the file range\n");
        return false;
    }

    uint32_t fileCRC;
    if (!file.seek(offset + size - sizeof(fileCRC))) {
        LOG_ERROR("failed to seek the file\n");
        return false;
    }

    size_t br;
    if (!file.read((char*)&fileCRC, sizeof(uint32_t), br)) {
        LOG_ERROR("failed to read the file\n");
        return false;
    }

    if (!file.seek(offset)) {
        LOG_ERROR("failed to seek the file\n");
        return false;
    }

    size -= sizeof(uint32_t);

    uint32_t crc = 0;
    uint8_t buff[256];
    while (size != 0) {
        uint32_t toRead = size;
        if (toRead > sizeof(buff)) {
            toRead = sizeof(buff);
        }

        size_t br;
        if (!file.read((char*)buff, toRead, br)) {
            LOG_ERROR("failed to read the file\n");
            return false;
        }

        crc = CRC32::calculate(buff, toRead, crc);

        size -= toRead;
    }

    return (crc == fileCRC);
}

/**
 * @brief Sanity-check the main header fields against policy and file size.
 *
 * Validates:
 *  - app name is NUL-terminated within APP_NAME_LEN,
 *  - supported libc version,
 *  - no flags outside FLAG_APP_ACCEPTED_MASK (every type value is valid),
 *  - fixed icon sizes,
 *  - service size range consistent with the file size and other sections.
 *
 * @param mainHeader Parsed @ref MainHeader structure.
 * @param fileSize   Full file size in bytes.
 * @return true if the header passes all checks; false otherwise.
 */
bool Validator::checkMainHeader(const App::MainHeader& mainHeader, uint32_t fileSize)
{
    if (strnlen(mainHeader.app_name, App::kNameLen) == App::kNameLen) {
        LOG_ERROR("File name is invalid\n");
        return false;
    }

    if (mainHeader.libcVersion != mLibCVersion) {
        LOG_ERROR("Unsuported LibC version 0x%08X, Expected 0x%08X\n",
                mainHeader.libcVersion, mLibCVersion);
        return false;
    }

    if (mainHeader.flags & ~FLAG_APP_ACCEPTED_MASK) {
        LOG_ERROR("Field flags is invalid\n");
        return false;
    }

    if (mainHeader.normal_icon_size != 3600) {
        LOG_ERROR("Incorrect normal icon size\n");
        return false;
    }

    if (mainHeader.small_icon_size != 900) {
        LOG_ERROR("Incorrect small icon size\n");
        return false;
    }

    uint32_t minServiceSize = fileSize -
                              sizeof(uint32_t) -
                              sizeof(mainHeader) -
                              mainHeader.normal_icon_size -
                              mainHeader.small_icon_size;

    if (mainHeader.serviceSize > minServiceSize ||
            mainHeader.serviceSize < sizeof(App::UappHeader))
    {
        LOG_ERROR("Service size is incorrect\n");
        return false;
    }

    return true;
}

/**
 * @brief Sanity-check an alias main header + payload against policy and file size.
 *
 * Validates:
 *  - app name is NUL-terminated within the name field,
 *  - FLAG_APP_VARIANT_ALIAS set, no flags outside FLAG_ALIAS_ACCEPTED_MASK,
 *  - fixed icon sizes,
 *  - serviceSize == 0 (an alias carries no code),
 *  - a known payload version,
 *  - configSize within bounds and the file size exactly matching the layout.
 *
 * No libc check: libcVersion is 0 by construction on aliases (see aliasIsValid).
 *
 * @param mainHeader Parsed @ref MainHeader structure.
 * @param payload    Parsed @ref VariantAliasPayload structure.
 * @param fileSize   Full file size in bytes.
 * @return true if the header passes all checks; false otherwise.
 */
bool Validator::checkAliasHeader(const App::MainHeader &mainHeader,
                                 const App::VariantAliasPayload &payload,
                                 uint32_t fileSize)
{
    if (strnlen(mainHeader.app_name, App::kNameLen) == App::kNameLen) {
        LOG_ERROR("File name is invalid\n");
        return false;
    }

    if (!App::IsAliasHeader(mainHeader)) {
        LOG_ERROR("Alias flag is missing\n");
        return false;
    }

    if (mainHeader.flags & ~FLAG_ALIAS_ACCEPTED_MASK) {
        LOG_ERROR("Field flags is invalid\n");
        return false;
    }

    if (mainHeader.normal_icon_size != App::kIconNormalBytes) {
        LOG_ERROR("Incorrect normal icon size\n");
        return false;
    }

    if (mainHeader.small_icon_size != App::kIconSmallBytes) {
        LOG_ERROR("Incorrect small icon size\n");
        return false;
    }

    if (mainHeader.serviceSize != 0) {
        LOG_ERROR("Alias must carry no code\n");
        return false;
    }

    if (payload.payloadVersion != App::kAliasPayloadVersion) {
        LOG_ERROR("Unsupported alias payload version %u\n", payload.payloadVersion);
        return false;
    }

    if (payload.configSize > App::kAliasConfigSizeMax) {
        LOG_ERROR("Alias config size is too big\n");
        return false;
    }

    if (fileSize != App::kAliasFixedSize + payload.configSize) {
        LOG_ERROR("Alias file size does not match its config size\n");
        return false;
    }

    return true;
}

/**
 * @brief Validate service and (optional) GUI user-app sections.
 *
 * Computes offsets/sizes of the service and GUI parts from the main header
 * and validates each via @ref checkUserApp.
 *
 * @param file       File interface.
 * @param mainHeader Parsed @ref MainHeader.
 * @return true if service (and GUI when present) pass validation; false otherwise.
 */
bool Validator::checkUserApps(Interface::IFile &file, const App::MainHeader &mainHeader)
{
    uint32_t fileSize = file.size();

    uint32_t serviceOffset = sizeof(App::MainHeader) +
                             mainHeader.normal_icon_size +
                             mainHeader.small_icon_size;

    if (!checkUserApp(file, serviceOffset, mainHeader.serviceSize)) {
        LOG_ERROR("The service userapp is invalid\n");
        return false;
    }

    uint32_t guiSize = fileSize -
                       sizeof(uint32_t) -
                       sizeof(mainHeader) -
                       mainHeader.normal_icon_size -
                       mainHeader.small_icon_size -
                       mainHeader.serviceSize;

    if (guiSize != 0) {
        if (!checkUserApp(file, serviceOffset + mainHeader.serviceSize, guiSize)) {
            LOG_ERROR("The GUI userapp is invalid\n");
            return false;
        }
    }

    return true;
}

/**
 * @brief Validate a single user-app section.
 *
 * Steps:
 *  - read and validate @ref UappHeader (including magic).
 *
 * Integrity of the section bytes is covered by the global CRC verified in
 * @ref isValid over the whole file, so no per-section CRC is checked here.
 *
 * @param file   File interface.
 * @param offset Section start offset in bytes.
 * @param size   Section size in bytes (retained for interface symmetry).
 * @return true if the section passes validation; false otherwise.
 *
 * @note This function seeks within the file to read the section header.
 */
bool Validator::checkUserApp(Interface::IFile &file, uint32_t offset, uint32_t size)
{
    (void)size;

    if (!file.seek(offset)) {
        LOG_ERROR("Failed to seek the file\n");
        return false;
    }

    App::UappHeader appHeader;
    size_t br;
    if (!file.read((char*)&appHeader, sizeof(App::UappHeader), br)) {
        LOG_ERROR("Failed to load the app header\n");
        return false;
    }

    if (appHeader.magic[0] != 'U' ||
        appHeader.magic[1] != 'A' ||
        appHeader.magic[2] != 'P' ||
        appHeader.magic[3] != 'P') {
        LOG_ERROR("Magic number is invalid\n");
        return false;
    }

    return true;
}

} // namespace App
