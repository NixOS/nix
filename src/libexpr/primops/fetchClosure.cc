#include "nix/expr/primops.hh"
#include "nix/store/store-open.hh"
#include "nix/store/realisation.hh"
#include "nix/store/make-content-addressed.hh"
#include "nix/util/url.hh"
#include "nix/util/environment-variables.hh"

namespace nix {

/**
 * Handler for the content addressed case.
 *
 * @param state Evaluator state and store to write to.
 * @param fromStore Store containing the path to rewrite.
 * @param fromPath Source path to be rewritten.
 * @param toPathMaybe Path to write the rewritten path to. If empty, the error shows the actual path.
 * @param v Return `Value`
 */
static void runFetchClosureWithRewrite(
    EvalState & state,
    const PosIdx pos,
    Store & fromStore,
    const StorePath & fromPath,
    const std::optional<StorePath> & toPathMaybe,
    Value & v)
{

    // establish toPath or throw

    if (!toPathMaybe || !state.store->isValidPath(*toPathMaybe)) {
        auto rewrittenPath = makeContentAddressed(fromStore, *state.store, fromPath);
        if (toPathMaybe && *toPathMaybe != rewrittenPath)
            throw Error(
                {.msg = HintFmt(
                     "rewriting '%s' to content-addressed form yielded '%s', while '%s' was expected",
                     state.store->printStorePath(fromPath),
                     state.store->printStorePath(rewrittenPath),
                     state.store->printStorePath(*toPathMaybe)),
                 .pos = state.positions[pos]});
        if (!toPathMaybe)
            throw Error(
                {.msg = HintFmt(
                     "rewriting '%s' to content-addressed form yielded '%s'\n"
                     "Use this value for the 'toPath' attribute passed to 'fetchClosure'",
                     state.store->printStorePath(fromPath),
                     state.store->printStorePath(rewrittenPath)),
                 .pos = state.positions[pos]});
    }

    const auto & toPath = *toPathMaybe;

    // check and return

    auto resultInfo = state.store->queryPathInfo(toPath);

    if (!resultInfo->isContentAddressed(*state.store)) {
        // We don't perform the rewriting when outPath already exists, as an optimisation.
        // However, we can quickly detect a mistake if the toPath is input addressed.
        throw Error(
            {.msg = HintFmt(
                 "The 'toPath' value '%s' is input-addressed, so it can't possibly be the result of rewriting to a content-addressed path.\n\n"
                 "Set 'toPath' to an empty string to make Nix report the correct content-addressed path.",
                 state.store->printStorePath(toPath)),
             .pos = state.positions[pos]});
    }

    state.allowClosure(toPath);

    state.mkStorePathString(toPath, v);
}

/**
 * Fetch the closure and make sure it's content addressed.
 */
static void runFetchClosureWithContentAddressedPath(
    EvalState & state, const PosIdx pos, Store & fromStore, const StorePath & fromPath, Value & v)
{

    if (!state.store->isValidPath(fromPath))
        copyClosure(fromStore, *state.store, RealisedPath::Set{fromPath});

    auto info = state.store->queryPathInfo(fromPath);

    if (!info->isContentAddressed(*state.store)) {
        throw Error(
            {.msg = HintFmt(
                 "The 'fromPath' value '%s' is input-addressed, but 'inputAddressed' is set to 'false' (default).\n\n"
                 "If you do intend to fetch an input-addressed store path, add\n\n"
                 "    inputAddressed = true;\n\n"
                 "to the 'fetchClosure' arguments.\n\n"
                 "Note that to ensure authenticity input-addressed store paths, users must configure a trusted binary cache public key on their systems. This is not needed for content-addressed paths.",
                 state.store->printStorePath(fromPath)),
             .pos = state.positions[pos]});
    }

    state.allowClosure(fromPath);

    state.mkStorePathString(fromPath, v);
}

/**
 * Fetch the closure and make sure it's input addressed.
 */
static void runFetchClosureWithInputAddressedPath(
    EvalState & state, const PosIdx pos, Store & fromStore, const StorePath & fromPath, Value & v)
{

    if (!state.store->isValidPath(fromPath))
        copyClosure(fromStore, *state.store, RealisedPath::Set{fromPath});

    auto info = state.store->queryPathInfo(fromPath);

    if (info->isContentAddressed(*state.store)) {
        throw Error(
            {.msg = HintFmt(
                 "The store object referred to by 'fromPath' at '%s' is not input-addressed, but 'inputAddressed' is set to 'true'.\n\n"
                 "Remove the 'inputAddressed' attribute (it defaults to 'false') to expect 'fromPath' to be content-addressed",
                 state.store->printStorePath(fromPath)),
             .pos = state.positions[pos]});
    }

    state.allowClosure(fromPath);

    state.mkStorePathString(fromPath, v);
}

typedef std::optional<StorePath> StorePathOrGap;

static void prim_fetchClosure(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.fetchClosure");

    std::optional<std::string> fromStoreUrl;
    std::optional<StorePath> fromPath;
    std::optional<StorePathOrGap> toPath;
    std::optional<bool> inputAddressedMaybe;

    for (auto & attr : *args[0]->attrs()) {
        const auto & attrName = state.symbols[attr.name];
        auto attrHint = [&]() -> std::string {
            return fmt("while evaluating the attribute '%s' passed to builtins.fetchClosure", attrName);
        };

        if (attrName == "fromPath") {
            NixStringContext context;
            fromPath = state.coerceToStorePath(attr.pos, *attr.value, context, attrHint());
        }

        else if (attrName == "toPath") {
            state.forceValue(*attr.value, attr.pos);
            bool isEmptyString = attr.value->type() == nString && attr.value->string_view() == "";
            if (isEmptyString) {
                toPath = StorePathOrGap{};
            } else {
                NixStringContext context;
                toPath = state.coerceToStorePath(attr.pos, *attr.value, context, attrHint());
            }
        }

        else if (attrName == "fromStore")
            fromStoreUrl = state.forceStringNoCtx(*attr.value, attr.pos, attrHint());

        else if (attrName == "inputAddressed")
            inputAddressedMaybe = state.forceBool(*attr.value, attr.pos, attrHint());

        else
            throw Error(
                {.msg = HintFmt("attribute '%s' isn't supported in call to 'fetchClosure'", attrName),
                 .pos = state.positions[pos]});
    }

    if (!fromPath)
        throw Error(
            {.msg = HintFmt("attribute '%s' is missing in call to 'fetchClosure'", "fromPath"),
             .pos = state.positions[pos]});

    bool inputAddressed = inputAddressedMaybe.value_or(false);

    if (inputAddressed) {
        if (toPath)
            throw Error(
                {.msg = HintFmt(
                     "attribute '%s' is set to true, but '%s' is also set. Please remove one of them",
                     "inputAddressed",
                     "toPath"),
                 .pos = state.positions[pos]});
    }

    if (!fromStoreUrl)
        throw Error(
            {.msg = HintFmt("attribute '%s' is missing in call to 'fetchClosure'", "fromStore"),
             .pos = state.positions[pos]});

    auto parsedURL = parseURL(*fromStoreUrl, /*lenient=*/true);

    if (parsedURL.scheme != "http" && parsedURL.scheme != "https"
        && !(getEnv("_NIX_IN_TEST").has_value() && parsedURL.scheme == "file"))
        throw Error(
            {.msg = HintFmt("'fetchClosure' only supports http:// and https:// stores"), .pos = state.positions[pos]});

    if (!parsedURL.query.empty())
        throw Error(
            {.msg = HintFmt("'fetchClosure' does not support URL query parameters (in '%s')", *fromStoreUrl),
             .pos = state.positions[pos]});

    auto fromStore = openStore(parsedURL.to_string());

    if (toPath)
        runFetchClosureWithRewrite(state, pos, *fromStore, *fromPath, *toPath, v);
    else if (inputAddressed)
        runFetchClosureWithInputAddressedPath(state, pos, *fromStore, *fromPath, v);
    else
        runFetchClosureWithContentAddressedPath(state, pos, *fromStore, *fromPath, v);
}

static RegisterPrimOp primop_fetchClosure({
    .name = "__fetchClosure",
    .args = {"args"},
    .doc = R"(
      Fetch a store path [closure](@docroot@/glossary.md#gloss-closure) from a binary cache, and return the store path as a string with context.

      This function can be invoked in three ways that we will discuss in order of preference.

      **Fetch a content-addressed store path**

      Example:

      ```nix
      builtins.fetchClosure {
        fromStore = "https://cache.nixos.org";
        fromPath = /nix/store/ldbhlwhh39wha58rm61bkiiwm6j7211j-git-2.33.1;
      }
      ```

      This is the simplest invocation, and it does not require the user of the expression to configure [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys) to ensure their authenticity.

      If your store path is [input addressed](@docroot@/glossary.md#gloss-input-addressed-store-object) instead of content addressed, consider the other two invocations.

      **Fetch any store path and rewrite it to a fully content-addressed store path**

      Example:

      ```nix
      builtins.fetchClosure {
        fromStore = "https://cache.nixos.org";
        fromPath = /nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1;
        toPath = /nix/store/ldbhlwhh39wha58rm61bkiiwm6j7211j-git-2.33.1;
      }
      ```

      This example fetches `/nix/store/r2jd...` from the specified binary cache,
      and rewrites it into the content-addressed store path
      `/nix/store/ldbh...`.

      Like the previous example, no extra configuration or privileges are required.

      To find out the correct value for `toPath` given a `fromPath`,
      use [`nix store make-content-addressed`](@docroot@/command-ref/new-cli/nix3-store-make-content-addressed.md):

      ```console
      # nix store make-content-addressed --from https://cache.nixos.org /nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1
      rewrote '/nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1' to '/nix/store/ldbhlwhh39wha58rm61bkiiwm6j7211j-git-2.33.1'
      ```

      Alternatively, set `toPath = ""` and find the correct `toPath` in the error message.

      **Fetch an input-addressed store path as is**

      Example:

      ```nix
      builtins.fetchClosure {
        fromStore = "https://cache.nixos.org";
        fromPath = /nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1;
        inputAddressed = true;
      }
      ```

      It is possible to fetch an [input-addressed store path](@docroot@/glossary.md#gloss-input-addressed-store-object) and return it as is.
      However, this is the least preferred way of invoking `fetchClosure`, because it requires that the input-addressed paths are trusted by the Nix configuration.

      **`builtins.storePath`**

      `fetchClosure` is similar to [`builtins.storePath`](#builtins-storePath) in that it allows you to use a previously built store path in a Nix expression.
      However, `fetchClosure` is more reproducible because it specifies a binary cache from which the path can be fetched.
      Also, using content-addressed store paths does not require users to configure [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys) to ensure their authenticity.
    )",
    .fun = prim_fetchClosure,
    .experimentalFeature = Xp::FetchClosure,
});

} // namespace nix
