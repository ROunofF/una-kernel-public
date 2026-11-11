/**
 ******************************************************************************
 * @file    AppHeaders.hpp
 * @brief   On-disk .uapp packaging structures and app flag definitions.
 * @details The packed headers shared by the loader, validator and manager,
 *          kept free of OS dependencies so host-side unit tests can compile
 *          against them directly.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef APP_HEADERS_HPP
#define APP_HEADERS_HPP

#include "AppTypes.hpp"

#include <cstdint>

#define FLAG_APP_TYPE_MASK          (0x00000003)
#define FLAG_APP_TYPE_ACTIVITY      (0)
#define FLAG_APP_TYPE_UTILITY       (1)
#define FLAG_APP_TYPE_GLANCE        (2)
#define FLAG_APP_TYPE_CLOCKFACE     (3)
#define FLAG_APP_AUTO_START         (0x00000008)
/// Unassigned, accepted and ignored. Kernels already in the field accept it
/// too, so this is the one bit a new flag can take and still load there.
#define FLAG_APP_RESERVED_IGNORED   (0x00000010)
/// Claims the app serves glance data from a non-Glance base type.
/// The kernel does not act on this: it was packed into every .uapp regardless
/// of type until SDK bb82a621, so the bit carries no information about apps
/// built before then and cannot be told apart from a truthful one. Glances are
/// selected by base type instead -- see Manager::startGlances().
#define FLAG_APP_HAS_GLANCE_INTF    (0x00000020)

/// Marks a code-less variant alias.
/// Deliberately outside FLAG_APP_ACCEPTED_MASK below, which is what kernels
/// already in the field accept too, so old kernels reject alias files instead
/// of loading them as apps.
#define FLAG_APP_VARIANT_ALIAS      (0x00000040)

/// Every bit the validator accepts in a real app's flags. Anything outside
/// it is reserved-must-be-zero and rejects the image. That includes the
/// unassigned 0x04, which kernels already in the field reject as well, so a
/// flag that must keep an image off those kernels goes at 0x04 or at 0x80
/// and above (0x40 is FLAG_APP_VARIANT_ALIAS).
#define FLAG_APP_ACCEPTED_MASK      (FLAG_APP_TYPE_MASK | FLAG_APP_AUTO_START | \
                                     FLAG_APP_RESERVED_IGNORED | FLAG_APP_HAS_GLANCE_INTF)

/// Every bit the validator accepts in a variant alias's flags.
#define FLAG_ALIAS_ACCEPTED_MASK    (FLAG_APP_ACCEPTED_MASK | FLAG_APP_VARIANT_ALIAS)

namespace App
{

#pragma pack(push, 1)

/**
 *  @brief  UAPP main header
 *  @verbatim
 *  Structure for packaging a binary file of the UNA application
 *  +-----------------------+-------+---------------------------------------------------+
 *  | Field name            | Size  | Meaning                                           |
 *  |                       | bytes |                                                   |
 *  | --------------------- | ----- | ------------------------------------------------- |
 *  | service_size          | 4     | Service code size only                            |
 *  | flags                 | 4     | App flags                                         |
 *  | name                  | 16    | Null-terminated string with app name              |
 *  | normal_icon_size      | 4     | Normal icon size                                  |
 *  | small_icon_size       | 4     | Small icon size                                   |
 *  | --------------------- | ----- | ------------------------------------------------- |
 *  | normal app_icon       | 3600  | App icon in ABGR2222 format                       |
 *  | small app_icon        | 900   | App icon in ABGR2222 format                       |
 *  +-----------------------+-------+---------------------------------------------------+
 *  @endverbatim
 */

struct MainHeader {
    uint64_t uappID;                    /**< User application ID */
    uint32_t uappVersion;               /**< User application version */
    uint32_t libcVersion;               /**< The version of LibC used by the user application */
    uint32_t serviceSize;               /**< Size of the app icon in words */
    uint32_t flags;                     /**< App flags */
    char     app_name[App::kNameLen];   /**< App name */
    uint32_t normal_icon_size;          /**< Normal icon size */
    uint32_t small_icon_size;           /**< Small icon size */
};

/**
 *  @brief  UAPP header Format structure
 *  @verbatim
 *  Structure for packaging a binary file of the UNA application
 *  +-----------------------+-------+---------------------------------------------------+
 *  | Field name            | Size  | Meaning                                           |
 *  |                       | bytes |                                                   |
 *  | --------------------- | ----- | ------------------------------------------------- |
 *  | magic                 | 4     | Always 'UAPP' to identify App                     |
 *  | version               | 1     | Version of this structure                         |
 *  |                       | 3     | reserved                                          |
 *  | major_version         | 1     | Major version of App (always zero)                |
 *  | minor_version         | 1     | Minor version of App (always zero)                |
 *  |                       | 6     | reserved                                          |
 *  | rel_addr_list_sz      | 4     | Size of list of addresses for relocation in words |
 *  | --------------------- | ----- | ------------------------------------------------- |
 *  | text_pads;            | 2     | Pads count after the .text section                |
 *  | preinit_array_pads;   | 2     | Pads count after the .preinit_array section       |
 *  | init_array_pads;      | 2     | Pads count after the .init_array section          |
 *  | fini_array_pads;      | 2     | Pads count after the .fini_array section          |
 *  | plt_pads;             | 2     | Pads count after the .plt section                 |
 *  | data_pads;            | 2     | Pads count after the .data section                |
 *  | got_pads;             | 2     | Pads count after the .got section                 |
 *  | bss_pads;             | 2     | Pads count after the .bss section                 |
 *  | --------------------- | ----- | ------------------------------------------------- |
 *  | text_sz               | 4     | .text section size in words                       |
 *  | preinit_array_sz      | 4     | .preinit_array section size in words              |
 *  | init_array_sz         | 4     | .init_array section size in words                 |
 *  | fini_array_sz         | 4     | .fini_array section size in words                 |
 *  | plt_sz                | 4     | .plt section size in words                        |
 *  | data_sz               | 4     | .data section size in words                       |
 *  | got_sz                | 4     | .got section size in words                        |
 *  | bss_sz                | 4     | .bss section size in words                        |
 *  | stack_sz              | 4     | .stack section size in words                      |
 *  |                       | 4     | reserved                                          |
 *  | --------------------- | ----- | ------------------------------------------------- |
 *  | rel_addr_list         |       | list of addresses for relocation                  |
 *  | .text section         |       | The .text section from the elf                    |
 *  | .init_array section   |       | The .init_array section from the elf              |
 *  | .fini_array section   |       | The .fini_array section from the elf              |
 *  | .plt section          |       | The .plt section from the elf                     |
 *  | .data section         |       | The .data section from the elf                    |
 *  | .got section          |       | The .got section from the elf                     |
 *  +-----------------------+-------+---------------------------------------------------+
 *  @endverbatim
 */

struct UappHeader {
    // Should be 'UAPP'. This is checked while loading the app and APP_INVALID

    uint8_t  magic[4];            /**< Magic number for validation */
    uint8_t  version;             /**< Version of this structure (max 255) */
    uint8_t  reserved1[3];        /**< Reserved */
    uint8_t  major_version;       /**< Major version of app (max 255) */
    uint8_t  minor_version;       /**< Minor version of app (max 255) */
    uint8_t  reserved2[6];        /**< Reserved */
    uint32_t rel_addr_list_sz;    /**< Size of the relocation address list in words */

    uint16_t text_pads;           /**< Pads count after the .text section */
    uint16_t preinit_array_pads;  /**< Pads count after the .preinit_array section */
    uint16_t init_array_pads;     /**< Pads count after the .init_array section */
    uint16_t fini_array_pads;     /**< Pads count after the .fini_array section */
    uint16_t plt_pads;            /**< Pads count after the .plt section */
    uint16_t data_pads;           /**< Pads count after the .data section */
    uint16_t got_pads;            /**< Pads count after the .got section */
    uint16_t bss_pads;            /**< Pads count after the .bss section */

    uint32_t text_sz;             /**< Size of the .text section in words */
    uint32_t preinit_array_sz;    /**< Size of the .preinit_array section in words */
    uint32_t init_array_sz;       /**< Size of the .init_array section in words */
    uint32_t fini_array_sz;       /**< Size of the .fini_array section in words */
    uint32_t plt_sz;              /**< Size of the .plt section in words */
    uint32_t data_sz;             /**< Size of the .data section in words */
    uint32_t got_sz;              /**< Size of the .got section in words */
    uint32_t bss_sz;              /**< Size of the .bss section in words */
    uint32_t stack_sz;            /**< Size of the ._user_stack section in words */

    uint8_t reserved3[4];           /** Reserved */

    /** The binary data is appended here */
};

/**
 *  @brief  Variant-alias payload of a code-less .uapp
 *  @verbatim
 *  An alias file is MainHeader + both icons + this payload + the embedded
 *  variant config + the standard trailing CRC32. It is discriminated from a
 *  real app by FLAG_APP_VARIANT_ALIAS (and carries serviceSize = 0,
 *  libcVersion = 0 -- the ABI is the target binary's property).
 *  +-----------------------+-------+---------------------------------------------------+
 *  | Field name            | Size  | Meaning                                           |
 *  |                       | bytes |                                                   |
 *  | --------------------- | ----- | ------------------------------------------------- |
 *  | payloadVersion        | 4     | This structure's version (1)                      |
 *  | targetAppID           | 8     | uappID of the base app this alias runs            |
 *  | minTargetVersion      | 4     | Minimum target uappVersion (0 = any)              |
 *  | origin                | 1     | 0 = shipped with the app, 1 = user-created        |
 *  | configSize            | 4     | Embedded variant config size in bytes             |
 *  | reserved              | 11    | Reserved, zero                                    |
 *  +-----------------------+-------+---------------------------------------------------+
 *  @endverbatim
 */

struct VariantAliasPayload {
    uint32_t payloadVersion;      /**< Payload structure version */
    uint64_t targetAppID;         /**< uappID of the base app (sole resolution key) */
    uint32_t minTargetVersion;    /**< Minimum target uappVersion, 0 = any */
    uint8_t  origin;              /**< 0 = shipped-with-app, 1 = user-created */
    uint32_t configSize;          /**< Embedded config size in bytes */
    uint8_t  reserved[11];        /**< Reserved for future extension */
};

#pragma pack(pop)

static_assert(sizeof(MainHeader) == 48, "MainHeader must stay 48 bytes (on-disk format)");
static_assert(sizeof(VariantAliasPayload) == 32, "VariantAliasPayload must stay 32 bytes (on-disk format)");

/// Current VariantAliasPayload::payloadVersion.
constexpr uint32_t kAliasPayloadVersion = 1;

/// Fixed icon payload sizes (ABGR2222, 60x60 and 30x30).
constexpr uint32_t kIconNormalBytes = App::kIconWH * App::kIconWH;
constexpr uint32_t kIconSmallBytes  = App::kIconSmallWH * App::kIconSmallWH;

/// File offset of the VariantAliasPayload in an alias .uapp.
constexpr uint32_t kAliasPayloadOffset =
        sizeof(MainHeader) + kIconNormalBytes + kIconSmallBytes;

/// Alias file size excluding the embedded config: header + icons + payload + CRC32.
constexpr uint32_t kAliasFixedSize =
        kAliasPayloadOffset + sizeof(VariantAliasPayload) + sizeof(uint32_t);

/// Sanity bound on the embedded variant config.
constexpr uint32_t kAliasConfigSizeMax = 8192;

/// True when the header marks a code-less variant alias rather than a real app.
inline bool IsAliasHeader(const MainHeader &header)
{
    return (header.flags & FLAG_APP_VARIANT_ALIAS) != 0;
}

/**
 * @brief   Base type an app's flags word states.
 * @details The whole of the type answer: the mask is two bits and all four
 *          values are assigned, so every input maps to a real type and
 *          Type::kUnknown is unreachable. The default arm is there because the
 *          argument is an integer, not an enum. No other flag bit takes part --
 *          a type is what an app is, not what it can do.
 * @param   flags: the MainHeader flags word.
 * @retval  The base type.
 */
inline Type TypeFromFlags(uint32_t flags)
{
    switch (flags & FLAG_APP_TYPE_MASK) {
        case FLAG_APP_TYPE_ACTIVITY:  return Type::kActivity;
        case FLAG_APP_TYPE_UTILITY:   return Type::kUtility;
        case FLAG_APP_TYPE_GLANCE:    return Type::kGlance;
        case FLAG_APP_TYPE_CLOCKFACE: return Type::kClockface;
        default:                      return Type::kUnknown;
    }
}

} // namespace App

#endif // APP_HEADERS_HPP
