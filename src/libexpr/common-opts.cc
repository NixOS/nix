#include "common-opts.hh"
#include "../libmain/shared.hh"
#include "util.hh"
#include "parser.hh"


namespace nix {


bool parseOptionArg(const string & arg, Strings::iterator & i,
    const Strings::iterator & argsEnd, EvalState & state,
    Bindings & autoArgs)
{
    if (arg != "--arg" && arg != "--argstr") return false;

    UsageError error(format("`%1%' requires two arguments") % arg);
    
    if (i == argsEnd) throw error;
    string name = *i++;
    if (i == argsEnd) throw error;
    string value = *i++;

    Value & v(autoArgs[state.symbols.create(name)].value);

    if (arg == "--arg")
        state.mkThunk_(v, parseExprFromString(state, value, absPath(".")));
    else
        mkString(v, value);
    
    return true;
}

 
}
