#include "common-eval-args.hh"
#include "shared.hh"
#include "filetransfer.hh"
#include "util.hh"
#include "eval.hh"
#include "fetchers.hh"
#include "store-api.hh"

namespace nix {

MixEvalArgs::MixEvalArgs()
{
    addFlag({
        .longName = "arg",
        .description = "argument to be passed to Nix functions",
        .labels = {"name", "expr"},
        .handler = {[&](std::string name, std::string expr) { autoArgs[name] = 'E' + expr; }}
    });

    addFlag({
        .longName = "argstr",
        .description = "string-valued argument to be passed to Nix functions",
        .labels = {"name", "string"},
        .handler = {[&](std::string name, std::string s) { autoArgs[name] = 'S' + s; }},
    });

    addFlag({
        .longName = "include",
        .shortName = 'I',
        .description = "add a path to the list of locations used to look up <...> file names",
        .labels = {"path"},
        .handler = {[&](std::string s) { searchPath.push_back(s); }}
    });
}

Bindings * MixEvalArgs::getAutoArgs(EvalState & state)
{
    Bindings * res = state.allocBindings(autoArgs.size());
    for (auto & i : autoArgs) {
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

Path lookupFileArg(EvalState & state, string s)
{
    if (isUri(s)) {
        return state.store->toRealPath(
            fetchers::downloadTarball(
                state.store, resolveUri(s), "source", false).storePath);
    } else if (s.size() > 2 && s.at(0) == '<' && s.at(s.size() - 1) == '>') {
        Path p = s.substr(1, s.size() - 2);
        return state.findFile(p);
    } else
        return absPath(s);
}

}
