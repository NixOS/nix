#include "primops.hh"
#include "store-api.hh"

namespace nix {

static void prim_fetchClosure(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);

    std::optional<std::string> fromStoreUrl;
    std::optional<StorePath> fromPath;

    for (auto & attr : *args[0]->attrs) {
        if (attr.name == "fromPath") {
            PathSet context;
            fromPath = state.coerceToStorePath(*attr.pos, *attr.value, context);
        }

        else if (attr.name == "fromStore")
            fromStoreUrl = state.forceStringNoCtx(*attr.value, *attr.pos);

        else
            throw Error({
                .msg = hintfmt("attribute '%s' isn't supported in call to 'fetchClosure'", attr.name),
                .errPos = pos
            });
    }

    if (!fromPath)
        throw Error({
            .msg = hintfmt("attribute '%s' is missing in call to 'fetchClosure'", "fromPath"),
            .errPos = pos
        });

    if (!fromStoreUrl)
        throw Error({
            .msg = hintfmt("attribute '%s' is missing in call to 'fetchClosure'", "fromStore"),
            .errPos = pos
        });

    // FIXME: only allow some "trusted" store types (like BinaryCacheStore).
    auto fromStore = openStore(*fromStoreUrl);

    copyClosure(*fromStore, *state.store, RealisedPath::Set { *fromPath });

    /* In pure mode, require a CA path. */
    if (evalSettings.pureEval) {
        auto info = state.store->queryPathInfo(*fromPath);
        if (!info->isContentAddressed(*state.store))
            throw Error({
                .msg = hintfmt("in pure mode, 'fetchClosure' requires a content-addressed path, which '%s' isn't",
                    state.store->printStorePath(*fromPath)),
                .errPos = pos
            });
    }

    auto fromPathS = state.store->printStorePath(*fromPath);
    v.mkString(fromPathS, {fromPathS});
}

static RegisterPrimOp primop_fetchClosure({
    .name = "__fetchClosure",
    .args = {"args"},
    .doc = R"(
    )",
    .fun = prim_fetchClosure,
});

}
