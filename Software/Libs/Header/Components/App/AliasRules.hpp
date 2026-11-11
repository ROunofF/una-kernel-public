/**
 ******************************************************************************
 * @file    AliasRules.hpp
 * @brief   Pure decision logic for variant-alias .uapp files.
 * @details Target resolution, flag sanitisation and the per-directory pick
 *          policy for Manager::scan(), factored out of the Manager so they
 *          can be unit-tested on the host (no OS or Backend dependencies).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef APP_ALIAS_RULES_HPP
#define APP_ALIAS_RULES_HPP

#include "IFileSystem.hpp"
#include "App/AppHeaders.hpp"

#include <cstddef>
#include <functional>

namespace App
{

/**
 * @brief   Read the VariantAliasPayload out of an alias .uapp.
 *
 * The payload sits at the fixed @ref kAliasPayloadOffset (after the main
 * header and both icons). Opens and closes the file itself, mirroring
 * Loader::ReadMainHeader.
 *
 * @param   file: Alias file (closed on entry).
 * @param   payload: Reference to store the payload.
 * @return  True if the payload was read successfully, otherwise false.
 */
bool ReadAliasPayload(Interface::IFile &file, App::VariantAliasPayload &payload);

/**
 * @brief   A registered app as seen by alias resolution.
 *
 * A flat view over Manager::mApps so the resolution rules need no
 * Application/Backend types.
 */
struct AliasTargetView {
    uint64_t appID     = 0;      ///< The entry's own uappID
    uint32_t version   = 0;      ///< The entry's uappVersion
    uint32_t flags     = 0;      ///< The entry's registered flags
    bool     isVariant = false;  ///< True when the entry is itself an alias
};

/**
 * @brief   Outcome of resolving an alias against the registered apps.
 */
struct AliasResolution {
    enum class Status {
        kOk,            ///< Resolved; targetIndex and flags are valid
        kNoTarget,      ///< No non-variant entry carries the target appID
        kTargetTooOld,  ///< Target found but older than minTargetVersion
        kIdCollision,   ///< The alias's own uappID is already registered --
                        ///< the registry's "id" must stay unique per entry
                        ///< (the phone's update/uninstall flows key on it)
        kGlanceTarget,  ///< Target is glance-typed: startGlances() runs every
                        ///< kGlance entry, so a glance variant would double-
                        ///< load the binary with no user intent (out of scope
                        ///< for v1, as autostart/glance flags already are)
    };

    Status   status = Status::kNoTarget;
    size_t   targetIndex = 0;     ///< Index into the targets view (kOk only)
    uint32_t flags = 0;           ///< Sanitised flags for the variant entry (kOk only)
    bool     typeAdjusted = false; ///< Type bits differed and were taken from the target
};

/**
 * @brief   Resolve an alias to its target and sanitise the variant's flags.
 *
 * Rules:
 *  - the alias's own uappID must not collide with any registered entry
 *    (real app or variant) -- registry "id" uniqueness is a phone contract;
 *  - the target is the first non-variant entry whose appID equals
 *    payload.targetAppID -- a variant can never be another variant's target;
 *  - a target older than payload.minTargetVersion rejects the alias
 *    (binary too old for this config);
 *  - a glance-typed target rejects the alias (glances of one binary would
 *    double-load it at glance start);
 *  - type bits differing from the target's are overridden by the target's
 *    (reported via typeAdjusted so the caller can log);
 *  - FLAG_APP_AUTO_START and FLAG_APP_HAS_GLANCE_INTF are force-cleared:
 *    auto-starting or glancing two instances of one binary is out of scope
 *    for v1. FLAG_APP_VARIANT_ALIAS itself is cleared from the registered
 *    flags -- Info::isVariant carries that state kernel-side.
 *
 * @param   alias: The alias file's main header.
 * @param   payload: The alias file's payload.
 * @param   targets: View over the registered apps (phase-1 scan results).
 * @param   count: Number of entries in targets.
 * @return  Resolution outcome; see @ref AliasResolution.
 */
AliasResolution ResolveAlias(const App::MainHeader &alias,
                             const App::VariantAliasPayload &payload,
                             const AliasTargetView *targets,
                             size_t count);

/// What PickUappInDir found in an app directory.
enum class DirPick {
    kNone,      ///< No candidate .uapp file
    kRealApp,   ///< A real (non-alias) app -- register via addApp()
    kAlias,     ///< Only alias file(s) -- register via addAlias() after phase 1
};

/**
 * @brief   Walk one app directory and pick the .uapp file to register.
 *
 * Determinism rule for pathological mixed directories: the first real
 * (non-alias) .uapp wins and stops the walk; otherwise the first
 * alias-flagged .uapp wins. A candidate whose header cannot be read is
 * treated as a real app so the addApp() path reports the failure, matching
 * the pre-alias behaviour of "the first .uapp decides".
 *
 * @param   dir: Opened directory to walk.
 * @param   dirPath: The directory's path with trailing '/', prepended to
 *          entry names to form candidate paths.
 * @param   extension: Required file extension (e.g. ".uapp").
 * @param   minFileSize: Entries at or below this size are skipped.
 * @param   readHeader: Reads the MainHeader of a candidate path; returns
 *          false when unreadable.
 * @param   pathOut: Receives the winning candidate's full path.
 * @param   pathOutSize: Size of pathOut in bytes.
 * @return  What was picked; pathOut is valid unless kNone.
 */
DirPick PickUappInDir(Interface::IDirectory &dir,
                      const char *dirPath,
                      const char *extension,
                      size_t minFileSize,
                      const std::function<bool(const char *path, App::MainHeader &header)> &readHeader,
                      char *pathOut,
                      size_t pathOutSize);

} // namespace App

#endif // APP_ALIAS_RULES_HPP
