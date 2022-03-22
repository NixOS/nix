#include "primops.hh"
#include "store-api.hh"
#include "make-content-addressed.hh"

namespace nix {

static void prim_fetchClosure(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);

    std::optional<std::string> fromStoreUrl;
    std::optional<StorePath> fromPath;
    bool toCA = false;
    std::optional<StorePath> toPath;

    for (auto & attr : *args[0]->attrs) {
        if (attr.name == "fromPath") {
            PathSet context;
            fromPath = state.coerceToStorePath(*attr.pos, *attr.value, context);
        }

        else if (attr.name == "toPath") {
            state.forceValue(*attr.value, *attr.pos);
            toCA = true;
            if (attr.value->type() != nString || attr.value->string.s != std::string("")) {
                PathSet context;
                toPath = state.coerceToStorePath(*attr.pos, *attr.value, context);
            }
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

    if (toCA) {
        if (!toPath || !state.store->isValidPath(*toPath)) {
            auto remappings = makeContentAddressed(*fromStore, *state.store, { *fromPath });
            auto i = remappings.find(*fromPath);
            assert(i != remappings.end());
            if (toPath && *toPath != i->second)
                throw Error({
                    .msg = hintfmt("rewriting '%s' to content-addressed form yielded '%s', while '%s' was expected",
                        state.store->printStorePath(*fromPath),
                        state.store->printStorePath(i->second),
                        state.store->printStorePath(*toPath)),
                    .errPos = pos
                });
            if (!toPath)
                throw Error({
                    .msg = hintfmt(
                        "rewriting '%s' to content-addressed form yielded '%s'; "
                        "please set this in the 'toPath' attribute passed to 'fetchClosure'",
                        state.store->printStorePath(*fromPath),
                        state.store->printStorePath(i->second)),
                    .errPos = pos
                });
        }
    } else {
        copyClosure(*fromStore, *state.store, RealisedPath::Set { *fromPath });
        toPath = fromPath;
    }

    /* In pure mode, require a CA path. */
    if (evalSettings.pureEval) {
        auto info = state.store->queryPathInfo(*toPath);
        if (!info->isContentAddressed(*state.store))
            throw Error({
                .msg = hintfmt("in pure mode, 'fetchClosure' requires a content-addressed path, which '%s' isn't",
                    state.store->printStorePath(*toPath)),
                .errPos = pos
            });
    }

    auto toPathS = state.store->printStorePath(*toPath);
    v.mkString(toPathS, {toPathS});
}

static RegisterPrimOp primop_fetchClosure({
    .name = "__fetchClosure",
    .args = {"args"},
    .doc = R"(
    )",
    .fun = prim_fetchClosure,
});

}
