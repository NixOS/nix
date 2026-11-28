#pragma once
///@file

#include "nix/util/args.hh"
#include "nix/util/canon-path.hh"
#include "nix/main/common-args.hh"
#include "nix/expr/search-path.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/store-reference.hh"

#include <filesystem>

namespace nix {

class Store;

namespace fetchers {
struct Settings;
}

class EvalState;
struct CompatibilitySettings;
class Bindings;

namespace flake {
struct Settings;
}

/**
 * @todo Get rid of global settings variables
 */
extern fetchers::Settings fetchSettings;

/**
 * @todo Get rid of global settings variables
 */
extern EvalSettings evalSettings;

/**
 * @todo Get rid of global settings variables
 */
extern flake::Settings flakeSettings;

/**
 * Settings that control behaviors that have changed since Nix 2.3.
 */
extern CompatibilitySettings compatibilitySettings;

struct MixEvalArgs : virtual Args, virtual MixRepair
{
    static constexpr auto category = "Common evaluation options";

    MixEvalArgs();

    const Bindings * getAutoArgs(EvalState & state);

    LookupPath lookupPath;

    std::optional<StoreReference> evalStoreUrl;

private:
    struct AutoArgExpr
    {
        std::string expr;
    };

    struct AutoArgString
    {
        std::string s;
    };

    struct AutoArgFile
    {
        std::filesystem::path path;
    };

    struct AutoArgStdin
    {};

    using AutoArg = std::variant<AutoArgExpr, AutoArgString, AutoArgFile, AutoArgStdin>;

    std::map<std::string, AutoArg> autoArgs;
};

/**
 * The path @p path on the host file system, as given on the command
 * line or in settings, in the evaluator's root filesystem.
 *
 * Resolved with `SourceAccessor::resolvePath`, so the result
 * never had any `..` removed lexically, and resolving its symlinks later
 * is sound. Nothing more is resolved, so that, for instance, `./.` in a
 * file named via a symlink is where the symlink is, not its target.
 *
 * @param baseDir Optional [base directory](https://nix.dev/manual/nix/development/glossary#gloss-base-directory),
 * for a relative @p path; by default, the current directory.
 *
 * @todo On Windows, a host path has a drive and `\` separators, which
 * this does not handle: it makes the whole path one component.
 */
SourcePath
lookupHostPath(EvalState & state, const std::filesystem::path & path, const std::filesystem::path * baseDir = nullptr);

/**
 * @param baseDir Optional [base directory](https://nix.dev/manual/nix/development/glossary#gloss-base-directory)
 */
SourcePath lookupFileArg(EvalState & state, std::string_view s, const std::filesystem::path * baseDir = nullptr);

} // namespace nix
