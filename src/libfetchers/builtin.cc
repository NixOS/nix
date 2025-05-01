#include "nix/store/builtins.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/archive.hh"

#include <nlohmann/json.hpp>

namespace nix {

static void builtinFetchTree(const BuiltinBuilderContext & ctx)
{
    auto out = get(ctx.drv.outputs, "out");
    if (!out)
        throw Error("'builtin:fetch-tree' requires an 'out' output");

    if (!(ctx.drv.type().isFixed() || ctx.drv.type().isImpure()))
        throw Error("'builtin:fetch-tree' must be a fixed-output or impure derivation");

    if (!ctx.structuredAttrs)
        throw Error("'builtin:fetch-tree' must have '__structuredAttrs = true'");

    setenv("NIX_CACHE_HOME", ctx.tmpDirInSandbox.c_str(), 1);

    using namespace fetchers;

    fetchers::Settings myFetchSettings;
    myFetchSettings.accessTokens = fetchSettings.accessTokens.get();

    // FIXME: disable use of the git/tarball cache

    auto input = Input::fromAttrs(myFetchSettings, jsonToAttrs((*ctx.structuredAttrs)["input"]));

    std::cerr << fmt("fetching '%s'...\n", input.to_string());

    /* Make sure we don't use the real store because we're in a forked
       process. */
    auto dummyStore = openStore("dummy://");

    auto [accessor, lockedInput] = input.getAccessor(dummyStore);

    auto source = sinkToSource([&](Sink & sink) { accessor->dumpPath(CanonPath::root, sink); });

    restorePath(ctx.outputs.at("out"), *source);
}

static RegisterBuiltinBuilder registerUnpackChannel("fetch-tree", builtinFetchTree);

}
