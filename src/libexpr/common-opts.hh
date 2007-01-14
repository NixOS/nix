#ifndef __COMMON_OPTS_H
#define __COMMON_OPTS_H

#include "eval.hh"


namespace nix {

/* Some common option parsing between nix-env and nix-instantiate. */
bool parseOptionArg(const string & arg, Strings::iterator & i,
    const Strings::iterator & argsEnd, EvalState & state,
    ATermMap & autoArgs);
    
}


#endif /* !__COMMON_OPTS_H */
