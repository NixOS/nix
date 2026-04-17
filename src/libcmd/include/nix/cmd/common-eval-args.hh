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

    Bindings * getAutoArgs(EvalState & state);

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
 * @param baseDir Optional [base directory](https://nix.dev/manual/nix/development/glossary#gloss-base-directory)
 */
SourcePath lookupFileArg(EvalState & state, std::string_view s, const std::filesystem::path * baseDir = nullptr);

/**
 * Fetch a tree reference (as accepted by the `--tree` CLI flag and by
 * [`builtins.fetchTree`](@docroot@/language/builtins.md#builtins-fetchTree))
 * into the store and return a `SourcePath` that can be passed to
 * `EvalState::evalFile`.
 *
 * This is the implementation shared between the new CLI's `--tree`
 * flag (in `SourceExprCommand`) and the legacy `nix-build` /
 * `nix-instantiate` commands.
 *
 * Requires the `fetch-tree` experimental feature.
 *
 * @param baseDir Base directory used to resolve relative path
 * references (typically the command base directory).
 */
SourcePath fetchTreeArg(EvalState & state, std::string_view treeRef, const std::filesystem::path & baseDir);

} // namespace nix
