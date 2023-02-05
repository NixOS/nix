#pragma once
///@file

#include "args.hh"
#include "canon-path.hh"
#include "common-args.hh"
#include "search-path.hh"

namespace nix {

class Store;
class EvalState;
class Bindings;
struct SourcePath;

struct MixEvalArgs : virtual MixRepair
{
    static constexpr auto category = "Common evaluation options";

    MixEvalArgs(AbstractArgs & args);

    Bindings * getAutoArgs(EvalState & state);

    SearchPath searchPath;

    std::optional<std::string> evalStoreUrl;

private:
    std::map<std::string, std::string> autoArgs;
};

SourcePath lookupFileArg(EvalState & state, std::string_view s, CanonPath baseDir = CanonPath::fromCwd());

}
