#include "nix/store/builtins.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/archive.hh"
#include "nix/store/filetransfer.hh"
#include "nix/store/store-open.hh"

#include <nlohmann/json.hpp>

namespace nix {

static void builtinFetchTree(const BuiltinBuilderContext & ctx)
{
    auto out = get(ctx.drv.outputs, "out");
    if (!out)
        throw Error("'builtin:fetch-tree' requires an 'out' output");

    if (!(ctx.drv.type().isFixed() || ctx.drv.type().isImpure()))
        throw Error("'builtin:fetch-tree' must be a fixed-output or impure derivation");

    if (!ctx.parsedDrv)
        throw Error("'builtin:fetch-tree' must have '__structuredAttrs = true'");

    setenv("NIX_CACHE_HOME", ctx.tmpDirInSandbox.c_str(), 1);

    using namespace fetchers;

    fetchers::Settings myFetchSettings;
    myFetchSettings.accessTokens = fetchSettings.accessTokens.get();

    // Make sure we don't use the FileTransfer object of the parent
    // since it's in a broken state after the fork. We also must not
    // delete it, so hang on to the shared_ptr.
    // FIXME: move FileTransfer into fetchers::Settings.
    auto prevFileTransfer = resetFileTransfer();

    // FIXME: disable use of the git/tarball cache

    auto input = Input::fromAttrs(myFetchSettings, jsonToAttrs(ctx.parsedDrv->structuredAttrs["input"]));

    std::cerr << fmt("fetching '%s'...\n", input.to_string());

    /* Make sure we don't use the real store because we're in a forked
       process. */
    auto dummyStore = openStore("dummy://");

    auto [accessor, lockedInput] = input.getAccessor(dummyStore);

    auto source = sinkToSource([&](Sink & sink) { accessor->dumpPath(CanonPath::root, sink); });

    restorePath(ctx.outputs.at("out"), *source);
}

static RegisterBuiltinBuilder registerUnpackChannel("fetch-tree", builtinFetchTree);

} // namespace nix
