#include "common-eval-args.hh"
#include "shared.hh"
#include "filetransfer.hh"
#include "util.hh"
#include "eval.hh"
#include "fetchers.hh"
#include "registry.hh"
#include "flake/flakeref.hh"
#include "store-api.hh"
#include "command.hh"

namespace nix {

MixEvalArgs::MixEvalArgs()
{
    auto category = "Common evaluation options";

    addFlag({
        .longName = "arg",
        .description = "Pass the value *expr* as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name", "expr"},
        .handler = {[&](std::string name, std::string expr) { autoArgs[name] = 'E' + expr; }}
    });

    addFlag({
        .longName = "argstr",
        .description = "Pass the string *string* as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name", "string"},
        .handler = {[&](std::string name, std::string s) { autoArgs[name] = 'S' + s; }},
    });

    addFlag({
        .longName = "include",
        .shortName = 'I',
        .description = "Add *path* to the list of locations used to look up `<...>` file names.",
        .category = category,
        .labels = {"path"},
        .handler = {[&](std::string s) { searchPath.push_back(s); }}
    });

    addFlag({
        .longName = "impure",
        .description = "Allow access to mutable paths and repositories.",
        .category = category,
        .handler = {[&]() {
            evalSettings.pureEval = false;
        }},
    });

    addFlag({
        .longName = "override-flake",
        .description = "Override the flake registries, redirecting *original-ref* to *resolved-ref*.",
        .category = category,
        .labels = {"original-ref", "resolved-ref"},
        .handler = {[&](std::string _from, std::string _to) {
            auto from = parseFlakeRef(_from, absPath("."));
            auto to = parseFlakeRef(_to, absPath("."));
            fetchers::Attrs extraAttrs;
            if (to.subdir != "") extraAttrs["dir"] = to.subdir;
            fetchers::overrideRegistry(from.input, to.input, extraAttrs);
        }},
        .completer = {[&](size_t, std::string_view prefix) {
            completeFlakeRef(openStore(), prefix);
        }}
    });

    addFlag({
        .longName = "eval-store",
        .description = "The Nix store to use for evaluations.",
        .category = category,
        .labels = {"store-url"},
        .handler = {&evalStoreUrl},
    });
}

Bindings * MixEvalArgs::getAutoArgs(EvalState & state)
{
    auto res = state.buildBindings(autoArgs.size());
    for (auto & i : autoArgs) {
        auto v = state.allocValue();
        if (i.second[0] == 'E')
            state.mkThunk_(*v, state.parseExprFromString(i.second.substr(1), absPath(".")));
        else
            v->mkString(((std::string_view) i.second).substr(1));
        res.insert(state.symbols.create(i.first), v);
    }
    return res.finish();
}

Path lookupFileArg(EvalState & state, std::string_view s)
{
    if (isUri(s)) {
        return state.store->toRealPath(
            state.store->makeFixedOutputPathFromCA(
                fetchers::downloadTarball(
                    state.store, resolveUri(s), "source", false).first.storePath));
    } else if (s.size() > 2 && s.at(0) == '<' && s.at(s.size() - 1) == '>') {
        Path p(s.substr(1, s.size() - 2));
        return state.findFile(p);
    } else
        return absPath(std::string(s));
}

}
