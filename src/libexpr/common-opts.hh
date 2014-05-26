#pragma once

#include "eval.hh"

namespace nix {

/* Some common option parsing between nix-env and nix-instantiate. */
bool parseOptionArg(const string & arg, Strings::iterator & i,
    const Strings::iterator & argsEnd, EvalState & state,
    Bindings & autoArgs);

bool parseSearchPathArg(const string & arg, Strings::iterator & i,
    const Strings::iterator & argsEnd, Strings & searchPath);

Path lookupFileArg(EvalState & state, string s);

}
