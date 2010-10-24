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

    /* !!! check for duplicates! */
    Value * v = state.allocValue();
    autoArgs.push_back(Attr(state.symbols.create(name), v));

    if (arg == "--arg")
        state.mkThunk_(*v, parseExprFromString(state, value, absPath(".")));
    else
        mkString(*v, value);

    autoArgs.sort(); // !!! inefficient

    return true;
}

 
}
