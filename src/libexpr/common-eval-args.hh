#pragma once

#include "args.hh"
#include "eval.hh"

namespace nix {

class Store;

template<class T>
struct Ptr;

struct MixEvalArgs : virtual Args
{
    MixEvalArgs();

    Ptr<Bindings> getAutoArgs(EvalState & state);

    Strings searchPath;

private:

    std::map<std::string, std::string> autoArgs;

    Ptr<Bindings> bindings;
};

Path lookupFileArg(EvalState & state, string s);

}
