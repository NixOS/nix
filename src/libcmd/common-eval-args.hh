#pragma once
///@file

#include "args.hh"
#include "canon-path.hh"
#include "common-args.hh"
#include "search-path.hh"

#include <filesystem>

namespace nix {

class Store;
class EvalState;
struct EvalSettings;
struct CompatibilitySettings;
class Bindings;
struct SourcePath;

/**
 * @todo Get rid of global setttings variables
 */
extern EvalSettings evalSettings;

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
    struct AutoArgExpr { std::string expr; };
    struct AutoArgString { std::string s; };
    struct AutoArgFile { std::filesystem::path path; };
    struct AutoArgStdin { };

    using AutoArg = std::variant<AutoArgExpr, AutoArgString, AutoArgFile, AutoArgStdin>;

    std::map<std::string, AutoArg> autoArgs;
};

/**
 * @param baseDir Optional [base directory](https://nixos.org/manual/nix/unstable/glossary#gloss-base-directory)
 */
SourcePath lookupFileArg(EvalState & state, PathView s, const Path * baseDir = nullptr);

}
