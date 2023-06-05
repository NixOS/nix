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
    bool enableRewriting = false;
    std::optional<StorePath> toPath;
    std::optional<bool> inputAddressedMaybe;

    for (auto & attr : *args[0]->attrs) {
        const auto & attrName = state.symbols[attr.name];
        auto attrHint = [&]() -> std::string {
            return "while evaluating the '" + attrName + "' attribute passed to builtins.fetchClosure";
        };

        if (attrName == "fromPath") {
            NixStringContext context;
            fromPath = state.coerceToStorePath(attr.pos, *attr.value, context, attrHint());
        }

        else if (attrName == "toPath") {
            state.forceValue(*attr.value, attr.pos);
            enableRewriting = true;
            if (attr.value->type() != nString || attr.value->string.s != std::string("")) {
                NixStringContext context;
                toPath = state.coerceToStorePath(attr.pos, *attr.value, context, attrHint());
            }
        }

        else if (attrName == "fromStore")
            fromStoreUrl = state.forceStringNoCtx(*attr.value, attr.pos,
                    attrHint());

        else if (attrName == "inputAddressed")
            inputAddressedMaybe = state.forceBool(*attr.value, attr.pos, attrHint());

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

    bool inputAddressed = inputAddressedMaybe.value_or(false);

    if (inputAddressed) {
        if (toPath)
            throw Error({
                .msg = hintfmt("attribute '%s' is set to true, but '%s' is also set. Please remove one of them",
                    "inputAddressed",
                    "toPath"),
                .errPos = state.positions[pos]
            });
        assert(!enableRewriting);
    }

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

    if (enableRewriting) {
        if (!toPath || !state.store->isValidPath(*toPath)) {
            auto rewrittenPath = makeContentAddressed(*fromStore, *state.store, *fromPath);
            if (toPath && *toPath != rewrittenPath)
                throw Error({
                    .msg = hintfmt("rewriting '%s' to content-addressed form yielded '%s', while '%s' was expected",
                        state.store->printStorePath(*fromPath),
                        state.store->printStorePath(rewrittenPath),
                        state.store->printStorePath(*toPath)),
                    .errPos = state.positions[pos]
                });
            if (!toPath)
                throw Error({
                    .msg = hintfmt(
                        "rewriting '%s' to content-addressed form yielded '%s'; "
                        "please set this in the 'toPath' attribute passed to 'fetchClosure'",
                        state.store->printStorePath(*fromPath),
                        state.store->printStorePath(rewrittenPath)),
                    .errPos = state.positions[pos]
                });
        }
    } else {
        if (!state.store->isValidPath(*fromPath))
            copyClosure(*fromStore, *state.store, RealisedPath::Set { *fromPath });
        toPath = fromPath;
    }

    auto info = state.store->queryPathInfo(*toPath);

    /* If inputAddressed is set, make sure the result is as expected. */
    if (inputAddressedMaybe) {
        bool expectCA = !inputAddressed;
        if (info->isContentAddressed(*state.store) != expectCA) {
            if (inputAddressed)
                throw Error({
                    .msg = hintfmt("The 'fetchClosure' result, '%s' is not input addressed, despite 'inputAddressed' being set to true. It is ok or even preferable to return a content addressed path, so you probably want to remove the 'inputAddressed' attribute",
                        state.store->printStorePath(*toPath)),
                    .errPos = state.positions[pos]
                });
            else
                throw Error({
                    .msg = hintfmt("The 'fetchClosure' result, '%s' is input addressed, despite 'inputAddressed' being set to false. Please make sure you passed the correct path(s) or set 'inputAddressed' to true if you intended to return an input addressed path",
                        state.store->printStorePath(*toPath)),
                    .errPos = state.positions[pos]
                });
        }
    } else {
        /*
          While it's fine to omit the inputAddressed attribute for CA paths,
          we want input addressing to be explicit, to inform readers and to give
          expression authors an opportunity to improve their user experience;
          see message below.
        */
        if (!info->isContentAddressed(*state.store)) {
            if (enableRewriting) {
                // We don't perform the rewriting when outPath already exists, as an optimisation.
                // However, we can quickly detect a mistake if the toPath is input addressed.
                // Ideally we'd compute the path for them here, but this error message is unlikely to occur in practice, so we keep it simple.
                throw Error({
                    .msg = hintfmt("The 'toPath' value '%s' is input addressed, so it can't possibly be the result of rewriting. You may set 'toPath' to an empty string to figure out the correct path.",
                        state.store->printStorePath(*toPath)),
                    .errPos = state.positions[pos]
                });
            } else {
                // We just checked toPath, but we report fromPath, because that's what the user certainly passed.
                assert (toPath == fromPath);
                throw Error({
                    .msg = hintfmt("The 'fromPath' value '%s' is input addressed, but input addressing was not requested. If you do intend to return an input addressed store path, add 'inputAddressed = true;' to the 'fetchClosure' arguments. Note that content addressing does not require users to configure a trusted binary cache public key on their systems, and is therefore preferred.",
                        state.store->printStorePath(*fromPath)),
                    .errPos = state.positions[pos]
                });
            }
        }
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
      allowing input addressing (`inputAddressed = true;`), then `toPath` may be
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
      Also, the default requirement of a content-addressed final store path
      avoids the need for users to configure [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys).

      This function is only available if you enable the experimental
      feature `fetch-closure`.
    )",
    .fun = prim_fetchClosure,
    .experimentalFeature = Xp::FetchClosure,
});

}
