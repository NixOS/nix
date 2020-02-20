#include "common-eval-args.hh"
#include "shared.hh"
#include "download.hh"
#include "util.hh"
#include "eval.hh"
#include "fetchers/registry.hh"
#include "flake/flakeref.hh"

namespace nix {

MixEvalArgs::MixEvalArgs()
{
    mkFlag()
        .longName("arg")
        .description("argument to be passed to Nix functions")
        .labels({"name", "expr"})
        .handler([&](std::vector<std::string> ss) { autoArgs[ss[0]] = 'E' + ss[1]; });

    mkFlag()
        .longName("argstr")
        .description("string-valued argument to be passed to Nix functions")
        .labels({"name", "string"})
        .handler([&](std::vector<std::string> ss) { autoArgs[ss[0]] = 'S' + ss[1]; });

    mkFlag()
        .shortName('I')
        .longName("include")
        .description("add a path to the list of locations used to look up <...> file names")
        .label("path")
        .handler([&](std::string s) { searchPath.push_back(s); });

    mkFlag()
        .longName("impure")
        .description("allow access to mutable paths and repositories")
        .handler([&](std::vector<std::string> ss) {
            evalSettings.pureEval = false;
        });

    mkFlag()
      .longName("override-flake")
      .labels({"original-ref", "resolved-ref"})
      .description("override a flake registry value")
      .arity(2)
      .handler([&](std::vector<std::string> ss) {
          auto from = parseFlakeRef(ss[0], absPath("."));
          auto to = parseFlakeRef(ss[1], absPath("."));
          fetchers::Input::Attrs extraAttrs;
          if (to.subdir != "") extraAttrs["subdir"] = to.subdir;
          fetchers::overrideRegistry(from.input, to.input, extraAttrs);
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
        CachedDownloadRequest request(s);
        request.unpack = true;
        return getDownloader()->downloadCached(state.store, request).path;
    } else if (s.size() > 2 && s.at(0) == '<' && s.at(s.size() - 1) == '>') {
        Path p = s.substr(1, s.size() - 2);
        return state.findFile(p);
    } else
        return absPath(s);
}

}
