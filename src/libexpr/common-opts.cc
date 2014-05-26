#include "common-opts.hh"
#include "../libmain/shared.hh"
#include "util.hh"


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
        state.mkThunk_(*v, state.parseExprFromString(value, absPath(".")));
    else
        mkString(*v, value);

    autoArgs.sort(); // !!! inefficient

    return true;
}


bool parseSearchPathArg(const string & arg, Strings::iterator & i,
    const Strings::iterator & argsEnd, Strings & searchPath)
{
    if (arg != "-I") return false;
    if (i == argsEnd) throw UsageError(format("`%1%' requires an argument") % arg);;
    searchPath.push_back(*i++);
    return true;
}


Path lookupFileArg(EvalState & state, string s)
{
    if (s.size() > 2 && s.at(0) == '<' && s.at(s.size() - 1) == '>') {
        Path p = s.substr(1, s.size() - 2);
        Path p2 = state.findFile(p);
        if (p2 == "") throw Error(format("file `%1%' was not found in the Nix search path (add it using $NIX_PATH or -I)") % p);
        return p2;
    } else
        return absPath(s);
}


}
