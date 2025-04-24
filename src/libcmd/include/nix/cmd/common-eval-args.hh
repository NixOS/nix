#pragma once
///@file

#include "nix/util/args.hh"
#include "nix/util/canon-path.hh"
#include "nix/main/common-args.hh"
#include "nix/expr/search-path.hh"

#include <filesystem>

namespace nix {

class Store;

namespace fetchers {
struct Settings;
}

class EvalState;
struct EvalSettings;
struct CompatibilitySettings;
class Bindings;
struct SourcePath;

namespace flake {
struct Settings;
}

/**
 * @todo Get rid of global setttings variables
 */
extern fetchers::Settings fetchSettings;

/**
 * @todo Get rid of global setttings variables
 */
extern EvalSettings evalSettings;

/**
 * @todo Get rid of global setttings variables
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

    std::optional<std::string> evalStoreUrl;

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
 * @param baseDir Optional [base directory](https://nixos.org/manual/nix/unstable/glossary#gloss-base-directory)
 */
SourcePath lookupFileArg(EvalState & state, std::string_view s, const Path * baseDir = nullptr);

}
