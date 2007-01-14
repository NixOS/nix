#include "common-opts.hh"
#include "../libmain/shared.hh"
#include "util.hh"
#include "parser.hh"


namespace nix {


bool parseOptionArg(const string & arg, Strings::iterator & i,
    const Strings::iterator & argsEnd, EvalState & state,
    ATermMap & autoArgs)
{
    if (arg != "--arg" && arg != "--argstr") return false;

    UsageError error(format("`%1%' requires two arguments") % arg);
    
    if (i == argsEnd) throw error;
    string name = *i++;
    if (i == argsEnd) throw error;
    string value = *i++;
    
    Expr e = arg == "--arg"
        ? parseExprFromString(state, value, absPath("."))
        : makeStr(value);
    autoArgs.set(toATerm(name), e);
    
    return true;
}

 
}
