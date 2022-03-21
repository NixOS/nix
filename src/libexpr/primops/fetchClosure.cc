#include "primops.hh"
#include "store-api.hh"

namespace nix {

static void prim_fetchClosure(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);

    std::optional<StorePath> storePath;
    std::optional<std::string> from;

    for (auto & attr : *args[0]->attrs) {
        if (attr.name == "storePath") {
            PathSet context;
            storePath = state.coerceToStorePath(*attr.pos, *attr.value, context);
        }

        else if (attr.name == "from")
            from = state.forceStringNoCtx(*attr.value, *attr.pos);

        else
            throw Error({
                .msg = hintfmt("attribute '%s' isn't supported in call to 'fetchClosure'", attr.name),
                .errPos = pos
            });
    }

    if (!storePath)
        throw Error({
            .msg = hintfmt("attribute '%s' is missing in call to 'fetchClosure'", "storePath"),
            .errPos = pos
        });

    if (!from)
        throw Error({
            .msg = hintfmt("attribute '%s' is missing in call to 'fetchClosure'", "from"),
            .errPos = pos
        });

    // FIXME: only allow some "trusted" store types (like BinaryCacheStore).
    auto srcStore = openStore(*from);

    copyClosure(*srcStore, *state.store, RealisedPath::Set { *storePath });

    /* In pure mode, require a CA path. */
    if (evalSettings.pureEval) {
        auto info = state.store->queryPathInfo(*storePath);
        if (!info->isContentAddressed(*state.store))
            throw Error({
                .msg = hintfmt("in pure mode, 'fetchClosure' requires a content-addressed path, which '%s' isn't",
                    state.store->printStorePath(*storePath)),
                .errPos = pos
            });
    }

    auto storePathS = state.store->printStorePath(*storePath);
    v.mkString(storePathS, {storePathS});
}

static RegisterPrimOp primop_fetchClosure({
    .name = "__fetchClosure",
    .args = {"args"},
    .doc = R"(
    )",
    .fun = prim_fetchClosure,
});

}
