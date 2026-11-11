/**
 ******************************************************************************
 * @file    UserAppValidator.hpp
 * @brief   Validation logic for User Applications (headers, CRC, sections)
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __APP_VALIDATOR_HPP
#define __APP_VALIDATOR_HPP


#include "IFileSystem.hpp"
#include "App/AppHeaders.hpp"

namespace App
{

class Validator {
public:
    Validator(uint32_t libCVersion);

    bool appIsValid(Interface::IFile &file);

    /**
     * @brief   Validate a code-less variant-alias .uapp end-to-end.
     *
     * Same open/close semantics as appIsValid. Checks the trailing CRC and
     * the alias-specific header/payload rules; deliberately performs NO libc
     * check (the alias carries libcVersion = 0 -- the ABI belongs to the
     * target binary, validated when the target itself was scanned) and never
     * parses the embedded config (the kernel validates its extent only).
     */
    bool aliasIsValid(Interface::IFile &file);

private:
    bool isValid(Interface::IFile &file);
    bool isValidAlias(Interface::IFile &file);

    bool checkCRC(Interface::IFile &file, uint32_t offset, uint32_t size);
    bool checkMainHeader(const App::MainHeader &mainHeader, uint32_t fileSize);
    bool checkAliasHeader(const App::MainHeader &mainHeader,
                          const App::VariantAliasPayload &payload,
                          uint32_t fileSize);
    bool checkUserApps(Interface::IFile &file, const App::MainHeader &mainHeader);
    bool checkUserApp(Interface::IFile &file, uint32_t offset, uint32_t size);

    const uint32_t mLibCVersion;
};

} // namespace App

#endif // __APP_VALIDATOR_HPP
