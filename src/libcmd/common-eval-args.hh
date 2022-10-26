#pragma once

#include "args.hh"

namespace nix {

class Store;
class EvalState;
class Bindings;
struct SourcePath;

struct MixEvalArgs : virtual Args
{
    static constexpr auto category = "Common evaluation options";

    MixEvalArgs();

    Bindings * getAutoArgs(EvalState & state);

    Strings searchPath;

    std::optional<std::string> evalStoreUrl;

private:
    std::map<std::string, std::string> autoArgs;
};

SourcePath lookupFileArg(EvalState & state, std::string_view s);

}
