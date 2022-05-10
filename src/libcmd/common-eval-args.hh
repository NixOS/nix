#pragma once

#include "args.hh"

namespace nix {

class Store;
class EvalState;
class Bindings;
struct SourcePath;

struct MixEvalArgs : virtual Args
{
    MixEvalArgs();

    Bindings * getAutoArgs(EvalState & state);

    Strings searchPath;

    std::optional<std::string> evalStoreUrl;

private:
    std::map<std::string, std::string> autoArgs;
};

SourcePath lookupFileArg(EvalState & state, std::string_view s);

}
