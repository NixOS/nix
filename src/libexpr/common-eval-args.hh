#pragma once

#include "args.hh"

namespace nix {

class Store;
class EvalState;
class Bindings;

struct MixEvalArgs : virtual Args
{
    MixEvalArgs();

    Bindings * getAutoArgs(EvalState & state);

    Strings searchPath;

private:

    std::map<std::string, std::string> autoArgs;
};

Path lookupFileArg(EvalState & state, string s);

}
