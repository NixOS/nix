#pragma once

#include "eval.hh"

namespace nix {

/* Some common option parsing between nix-env and nix-instantiate. */
bool parseAutoArgs(Strings::iterator & i,
    const Strings::iterator & argsEnd, std::map<string, string> & res);

Bindings * evalAutoArgs(EvalState & state, std::map<string, string> & in);

bool parseSearchPathArg(Strings::iterator & i,
    const Strings::iterator & argsEnd, Strings & searchPath);

Path lookupFileArg(EvalState & state, string s);

}
