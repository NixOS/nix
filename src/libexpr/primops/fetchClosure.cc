#include "primops.hh"
#include "store-api.hh"
#include "make-content-addressed.hh"
#include "url.hh"

namespace nix {

static void prim_fetchClosure(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.fetchClosure");

    std::optional<std::string> fromStoreUrl;
    std::optional<StorePath> fromPath;
    bool toCA = false;
    std::optional<StorePath> toPath;

    for (auto & attr : *args[0]->attrs) {
        const auto & attrName = state.symbols[attr.name];

        if (attrName == "fromPath") {
            NixStringContext context;
            fromPath = state.coerceToStorePath(attr.pos, *attr.value, context,
                    "while evaluating the 'fromPath' attribute passed to builtins.fetchClosure");
        }

        else if (attrName == "toPath") {
            state.forceValue(*attr.value, attr.pos);
            toCA = true;
            if (attr.value->type() != nString || attr.value->string.s != std::string("")) {
                NixStringContext context;
                toPath = state.coerceToStorePath(attr.pos, *attr.value, context,
                        "while evaluating the 'toPath' attribute passed to builtins.fetchClosure");
            }
        }

        else if (attrName == "fromStore")
            fromStoreUrl = state.forceStringNoCtx(*attr.value, attr.pos,
                    "while evaluating the 'fromStore' attribute passed to builtins.fetchClosure");

        else
            throw Error({
                .msg = hintfmt("attribute '%s' isn't supported in call to 'fetchClosure'", attrName),
                .errPos = state.positions[pos]
            });
    }

    if (!fromPath)
        throw Error({
            .msg = hintfmt("attribute '%s' is missing in call to 'fetchClosure'", "fromPath"),
            .errPos = state.positions[pos]
        });

    if (!fromStoreUrl)
        throw Error({
            .msg = hintfmt("attribute '%s' is missing in call to 'fetchClosure'", "fromStore"),
            .errPos = state.positions[pos]
        });

    auto parsedURL = parseURL(*fromStoreUrl);

    if (parsedURL.scheme != "http" &&
        parsedURL.scheme != "https" &&
        !(getEnv("_NIX_IN_TEST").has_value() && parsedURL.scheme == "file"))
        throw Error({
            .msg = hintfmt("'fetchClosure' only supports http:// and https:// stores"),
            .errPos = state.positions[pos]
        });

    if (!parsedURL.query.empty())
        throw Error({
            .msg = hintfmt("'fetchClosure' does not support URL query parameters (in '%s')", *fromStoreUrl),
            .errPos = state.positions[pos]
        });

    auto fromStore = openStore(parsedURL.to_string());

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
                    .errPos = state.positions[pos]
                });
            if (!toPath)
                throw Error({
                    .msg = hintfmt(
                        "rewriting '%s' to content-addressed form yielded '%s'; "
                        "please set this in the 'toPath' attribute passed to 'fetchClosure'",
                        state.store->printStorePath(*fromPath),
                        state.store->printStorePath(i->second)),
                    .errPos = state.positions[pos]
                });
        }
    } else {
        if (!state.store->isValidPath(*fromPath))
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
                .errPos = state.positions[pos]
            });
    }

    state.mkStorePathString(*toPath, v);
}

static RegisterPrimOp primop_fetchClosure({
    .name = "__fetchClosure",
    .args = {"args"},
    .doc = R"(
      Fetch a Nix store closure from a binary cache, rewriting it into
      content-addressed form. For example,

      ```nix
      builtins.fetchClosure {
        fromStore = "https://cache.nixos.org";
        fromPath = /nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1;
        toPath = /nix/store/ldbhlwhh39wha58rm61bkiiwm6j7211j-git-2.33.1;
      }
      ```

      fetches `/nix/store/r2jd...` from the specified binary cache,
      and rewrites it into the content-addressed store path
      `/nix/store/ldbh...`.

      If `fromPath` is already content-addressed, or if you are
      allowing impure evaluation (`--impure`), then `toPath` may be
      omitted.

      To find out the correct value for `toPath` given a `fromPath`,
      you can use `nix store make-content-addressed`:

      ```console
      # nix store make-content-addressed --from https://cache.nixos.org /nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1
      rewrote '/nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1' to '/nix/store/ldbhlwhh39wha58rm61bkiiwm6j7211j-git-2.33.1'
      ```

      This function is similar to `builtins.storePath` in that it
      allows you to use a previously built store path in a Nix
      expression. However, it is more reproducible because it requires
      specifying a binary cache from which the path can be fetched.
      Also, requiring a content-addressed final store path avoids the
      need for users to configure binary cache public keys.
    )",
    .fun = prim_fetchClosure,
    .experimentalFeature = Xp::FetchClosure,
});

}
