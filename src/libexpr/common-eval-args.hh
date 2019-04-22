#pragma once

#include "args.hh"

namespace nix {

class Store;
class EvalState;
class Bindings;

template<class T>
struct Ptr;

struct MixEvalArgs : virtual Args
{
    MixEvalArgs();

    Ptr<Bindings> getAutoArgs(EvalState & state);

    Strings searchPath;

private:

    std::map<std::string, std::string> autoArgs;
};

Path lookupFileArg(EvalState & state, string s);

}
