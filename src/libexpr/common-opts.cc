#include "common-opts.hh"
#include "shared.hh"
#include "download.hh"
#include "util.hh"


namespace nix {


bool parseAutoArgs(Strings::iterator & i,
    const Strings::iterator & argsEnd, std::map<string, string> & res)
{
    string arg = *i;
    if (arg != "--arg" && arg != "--argstr") return false;

    UsageError error(format("‘%1%’ requires two arguments") % arg);

    if (++i == argsEnd) throw error;
    string name = *i;
    if (++i == argsEnd) throw error;
    string value = *i;

    res[name] = (arg == "--arg" ? 'E' : 'S') + value;

    return true;
}


Bindings * evalAutoArgs(EvalState & state, std::map<string, string> & in)
{
    Bindings * res = state.allocBindings(in.size());
    for (auto & i : in) {
        Value * v = state.allocValue();
        if (i.second[0] == 'E')
            state.mkThunk_(*v, state.parseExprFromString(string(i.second, 1), absPath(".")));
        else
            mkString(*v, string(i.second, 1));
        res->push_back(Attr(state.symbols.create(i.first), v));
    }
    res->sort();
    return res;
}


bool parseSearchPathArg(Strings::iterator & i,
    const Strings::iterator & argsEnd, Strings & searchPath)
{
    if (*i != "-I") return false;
    if (++i == argsEnd) throw UsageError("‘-I’ requires an argument");
    searchPath.push_back(*i);
    return true;
}


Path lookupFileArg(EvalState & state, string s)
{
    if (isUri(s))
        return downloadFileCached(s, true);
    else if (s.size() > 2 && s.at(0) == '<' && s.at(s.size() - 1) == '>') {
        Path p = s.substr(1, s.size() - 2);
        return state.findFile(p);
    } else
        return absPath(s);
}


}
