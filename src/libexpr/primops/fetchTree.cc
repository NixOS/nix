#include "nix/fetchers/attrs.hh"
#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/store-api.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/store/filetransfer.hh"
#include "nix/fetchers/registry.hh"
#include "nix/fetchers/tarball.hh"
#include "nix/util/url.hh"
#include "nix/expr/value-to-json.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/fetchers/input-cache.hh"

#include <nlohmann/json.hpp>

#include <ctime>
#include <iomanip>
#include <regex>

namespace nix {

void emitTreeAttrs(
    EvalState & state,
    const StorePath & storePath,
    const fetchers::Input & input,
    Value & v,
    bool emptyRevFallback,
    bool forceDirty)
{
    auto attrs = state.buildBindings(100);

    state.mkStorePathString(storePath, attrs.alloc(state.s.outPath));

    // FIXME: support arbitrary input attributes.

    if (auto narHash = input.getNarHash())
        attrs.alloc("narHash").mkString(narHash->to_string(HashFormat::SRI, true));

    if (input.getType() == "git")
        attrs.alloc("submodules").mkBool(fetchers::maybeGetBoolAttr(input.attrs, "submodules").value_or(false));

    if (!forceDirty) {

        if (auto rev = input.getRev()) {
            attrs.alloc("rev").mkString(rev->gitRev());
            attrs.alloc("shortRev").mkString(rev->gitShortRev());
        } else if (emptyRevFallback) {
            // Backwards compat for `builtins.fetchGit`: dirty repos return an empty sha1 as rev
            auto emptyHash = Hash(HashAlgorithm::SHA1);
            attrs.alloc("rev").mkString(emptyHash.gitRev());
            attrs.alloc("shortRev").mkString(emptyHash.gitShortRev());
        }

        if (auto revCount = input.getRevCount())
            attrs.alloc("revCount").mkInt(*revCount);
        else if (emptyRevFallback)
            attrs.alloc("revCount").mkInt(0);
    }

    if (auto dirtyRev = fetchers::maybeGetStrAttr(input.attrs, "dirtyRev")) {
        attrs.alloc("dirtyRev").mkString(*dirtyRev);
        attrs.alloc("dirtyShortRev").mkString(*fetchers::maybeGetStrAttr(input.attrs, "dirtyShortRev"));
    }

    if (auto lastModified = input.getLastModified()) {
        attrs.alloc("lastModified").mkInt(*lastModified);
        attrs.alloc("lastModifiedDate").mkString(fmt("%s", std::put_time(std::gmtime(&*lastModified), "%Y%m%d%H%M%S")));
    }

    v.mkAttrs(attrs);
}

struct FetchTreeParams
{
    bool emptyRevFallback = false;
    bool allowNameArgument = false;
    bool isFetchGit = false;
    bool isFinal = false;
};

static void fetchTree(
    EvalState & state, const PosIdx pos, Value ** args, Value & v, const FetchTreeParams & params = FetchTreeParams{})
{
    fetchers::Input input{state.fetchSettings};
    NixStringContext context;
    std::optional<std::string> type;
    auto fetcher = params.isFetchGit ? "fetchGit" : "fetchTree";
    if (params.isFetchGit)
        type = "git";

    state.forceValue(*args[0], pos);

    if (args[0]->type() == nAttrs) {
        state.forceAttrs(*args[0], pos, fmt("while evaluating the argument passed to '%s'", fetcher));

        fetchers::Attrs attrs;

        if (auto aType = args[0]->attrs()->get(state.s.type)) {
            if (type)
                state.error<EvalError>("unexpected argument 'type'").atPos(pos).debugThrow();
            type = state.forceStringNoCtx(
                *aType->value, aType->pos, fmt("while evaluating the `type` argument passed to '%s'", fetcher));
        } else if (!type)
            state.error<EvalError>("argument 'type' is missing in call to '%s'", fetcher).atPos(pos).debugThrow();

        attrs.emplace("type", type.value());

        for (auto & attr : *args[0]->attrs()) {
            if (attr.name == state.s.type)
                continue;
            state.forceValue(*attr.value, attr.pos);
            if (attr.value->type() == nPath || attr.value->type() == nString) {
                auto s = state.coerceToString(attr.pos, *attr.value, context, "", false, false).toOwned();
                attrs.emplace(
                    state.symbols[attr.name],
                    params.isFetchGit && state.symbols[attr.name] == "url" ? fixGitURL(s).to_string() : s);
            } else if (attr.value->type() == nBool)
                attrs.emplace(state.symbols[attr.name], Explicit<bool>{attr.value->boolean()});
            else if (attr.value->type() == nInt) {
                auto intValue = attr.value->integer().value;

                if (intValue < 0)
                    state
                        .error<EvalError>(
                            "negative value given for '%s' argument '%s': %d",
                            fetcher,
                            state.symbols[attr.name],
                            intValue)
                        .atPos(pos)
                        .debugThrow();

                attrs.emplace(state.symbols[attr.name], uint64_t(intValue));
            } else if (state.symbols[attr.name] == "publicKeys") {
                experimentalFeatureSettings.require(Xp::VerifiedFetches);
                attrs.emplace(
                    state.symbols[attr.name], printValueAsJSON(state, true, *attr.value, pos, context).dump());
            } else
                state
                    .error<TypeError>(
                        "argument '%s' to '%s' is %s while a string, Boolean or integer is expected",
                        state.symbols[attr.name],
                        fetcher,
                        showType(*attr.value))
                    .debugThrow();
        }

        if (params.isFetchGit && !attrs.contains("exportIgnore")
            && (!attrs.contains("submodules") || !*fetchers::maybeGetBoolAttr(attrs, "submodules"))) {
            attrs.emplace("exportIgnore", Explicit<bool>{true});
        }

        // fetchTree should fetch git repos with shallow = true by default
        if (type == "git" && !params.isFetchGit && !attrs.contains("shallow")) {
            attrs.emplace("shallow", Explicit<bool>{true});
        }

        if (!params.allowNameArgument)
            if (auto nameIter = attrs.find("name"); nameIter != attrs.end())
                state.error<EvalError>("argument 'name' isn’t supported in call to '%s'", fetcher)
                    .atPos(pos)
                    .debugThrow();

        input = fetchers::Input::fromAttrs(state.fetchSettings, std::move(attrs));
    } else {
        auto url = state
                       .coerceToString(
                           pos,
                           *args[0],
                           context,
                           fmt("while evaluating the first argument passed to '%s'", fetcher),
                           false,
                           false)
                       .toOwned();

        if (params.isFetchGit) {
            fetchers::Attrs attrs;
            attrs.emplace("type", "git");
            attrs.emplace("url", fixGitURL(url).to_string());
            if (!attrs.contains("exportIgnore")
                && (!attrs.contains("submodules") || !*fetchers::maybeGetBoolAttr(attrs, "submodules"))) {
                attrs.emplace("exportIgnore", Explicit<bool>{true});
            }
            input = fetchers::Input::fromAttrs(state.fetchSettings, std::move(attrs));
        } else {
            if (!experimentalFeatureSettings.isEnabled(Xp::Flakes))
                state
                    .error<EvalError>(
                        "passing a string argument to '%s' requires the 'flakes' experimental feature", fetcher)
                    .atPos(pos)
                    .debugThrow();
            input = fetchers::Input::fromURL(state.fetchSettings, url);
        }
    }

    if (!state.settings.pureEval && !input.isDirect() && experimentalFeatureSettings.isEnabled(Xp::Flakes))
        input = lookupInRegistries(state.store, input, fetchers::UseRegistries::Limited).first;

    if (state.settings.pureEval && !input.isLocked()) {
        if (input.getNarHash())
            warn(
                "Input '%s' is unlocked (e.g. lacks a Git revision) but does have a NAR hash. "
                "This is deprecated since such inputs are verifiable but may not be reproducible.",
                input.to_string());
        else
            state
                .error<EvalError>(
                    "in pure evaluation mode, '%s' doesn't fetch unlocked input '%s'", fetcher, input.to_string())
                .atPos(pos)
                .debugThrow();
    }

    state.checkURI(input.toURLString());

    if (params.isFinal) {
        input.attrs.insert_or_assign("__final", Explicit<bool>(true));
    } else {
        if (input.isFinal())
            throw Error("input '%s' is not allowed to use the '__final' attribute", input.to_string());
    }

    auto cachedInput = state.inputCache->getAccessor(state.store, input, fetchers::UseRegistries::No);

    auto storePath = state.mountInput(cachedInput.lockedInput, input, cachedInput.accessor);

    emitTreeAttrs(state, storePath, cachedInput.lockedInput, v, params.emptyRevFallback, false);
}

static void prim_fetchTree(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    fetchTree(state, pos, args, v, {});
}

static RegisterPrimOp primop_fetchTree({
    .name = "fetchTree",
    .args = {"input"},
    .doc = R"(
      Fetch a file system tree or a plain file using one of the supported backends and return an attribute set with:

      - the resulting fixed-output [store path](@docroot@/store/store-path.md)
      - the corresponding [NAR](@docroot@/store/file-system-object/content-address.md#serial-nix-archive) hash
      - backend-specific metadata (currently not documented). <!-- TODO: document output attributes -->

      *input* must be an attribute set with the following attributes:

      - `type` (String, required)

        One of the [supported source types](#source-types).
        This determines other required and allowed input attributes.

      - `narHash` (String, optional)

        The `narHash` parameter can be used to substitute the source of the tree.
        It also allows for verification of tree contents that may not be provided by the underlying transfer mechanism.
        If `narHash` is set, the source is first looked up is the Nix store and [substituters](@docroot@/command-ref/conf-file.md#conf-substituters), and only fetched if not available.

      A subset of the output attributes of `fetchTree` can be re-used for subsequent calls to `fetchTree` to produce the same result again.
      That is, `fetchTree` is idempotent.

      Downloads are cached in `$XDG_CACHE_HOME/nix`.
      The remote source is fetched from the network if both are true:
      - A NAR hash is supplied and the corresponding store path is not [valid](@docroot@/glossary.md#gloss-validity), that is, not available in the store

        > **Note**
        >
        > [Substituters](@docroot@/command-ref/conf-file.md#conf-substituters) are not used in fetching.

      - There is no cache entry or the cache entry is older than [`tarball-ttl`](@docroot@/command-ref/conf-file.md#conf-tarball-ttl)

      ## Source types

      The following source types and associated input attributes are supported.

      <!-- TODO: It would be soooo much more predictable to work with (and
      document) if `fetchTree` was a curried call with the first parameter for
      `type` or an attribute like `builtins.fetchTree.git`! -->

      - `"file"`

        Place a plain file into the Nix store.
        This is similar to [`builtins.fetchurl`](@docroot@/language/builtins.md#builtins-fetchurl)

        - `url` (String, required)

          Supported protocols:

          - `https`

            > **Example**
            >
            > ```nix
            > fetchTree {
            >   type = "file";
            >   url = "https://example.com/index.html";
            > }
            > ```

          - `http`

            Insecure HTTP transfer for legacy sources.

            > **Warning**
            >
            > HTTP performs no encryption or authentication.
            > Use a `narHash` known in advance to ensure the output has expected contents.

          - `file`

            A file on the local file system.

            > **Example**
            >
            > ```nix
            > fetchTree {
            >   type = "file";
            >   url = "file:///home/eelco/nix/README.md";
            > }
            > ```

      - `"tarball"`

        Download a tar archive and extract it into the Nix store.
        This has the same underlying implementation as [`builtins.fetchTarball`](@docroot@/language/builtins.md#builtins-fetchTarball)

        - `url` (String, required)

           > **Example**
           >
           > ```nix
           > fetchTree {
           >   type = "tarball";
           >   url = "https://github.com/NixOS/nixpkgs/tarball/nixpkgs-23.11";
           > }
           > ```

      - `"git"`

        Fetch a Git tree and copy it to the Nix store.
        This is similar to [`builtins.fetchGit`](@docroot@/language/builtins.md#builtins-fetchGit).

        - `url` (String, required)

          The URL formats supported are the same as for Git itself.

          > **Example**
          >
          > ```nix
          > fetchTree {
          >   type = "git";
          >   url = "git@github.com:NixOS/nixpkgs.git";
          > }
          > ```

          > **Note**
          >
          > If the URL points to a local directory, and no `ref` or `rev` is given, Nix only considers files added to the Git index, as listed by `git ls-files` but use the *current file contents* of the Git working directory.

        - `ref` (String, optional)

          By default, this has no effect. This becomes relevant only once `shallow` cloning is disabled.

          A [Git reference](https://git-scm.com/book/en/v2/Git-Internals-Git-References), such as a branch or tag name.

          Default: `"HEAD"`

        - `rev` (String, optional)

          A Git revision; a commit hash.

          Default: the tip of `ref`

        - `shallow` (Bool, optional)

          Make a shallow clone when fetching the Git tree.
          When this is enabled, the options `ref` and `allRefs` have no effect anymore.

          Default: `true`

        - `submodules` (Bool, optional)

          Also fetch submodules if available.

          Default: `false`

        - `lfs` (Bool, optional)

          Fetch any [Git LFS](https://git-lfs.com/) files.

          Default: `false`

        - `allRefs` (Bool, optional)

          By default, this has no effect. This becomes relevant only once `shallow` cloning is disabled.

          Whether to fetch all references (eg. branches and tags) of the repository.
          With this argument being true, it's possible to load a `rev` from *any* `ref`.
          (Without setting this option, only `rev`s from the specified `ref` are supported).

          Default: `false`

        - `lastModified` (Integer, optional)

          Unix timestamp of the fetched commit.

          If set, pass through the value to the output attribute set.
          Otherwise, generated from the fetched Git tree.

        - `revCount` (Integer, optional)

          Number of revisions in the history of the Git repository before the fetched commit.

          If set, pass through the value to the output attribute set.
          Otherwise, generated from the fetched Git tree.

      The following input types are still subject to change:

      - `"path"`
      - `"github"`
      - `"gitlab"`
      - `"sourcehut"`
      - `"mercurial"`

     *input* can also be a [URL-like reference](@docroot@/command-ref/new-cli/nix3-flake.md#flake-references).
     The additional input types and the URL-like syntax requires the [`flakes` experimental feature](@docroot@/development/experimental-features.md#xp-feature-flakes) to be enabled.

      > **Example**
      >
      > Fetch a GitHub repository using the attribute set representation:
      >
      > ```nix
      > builtins.fetchTree {
      >   type = "github";
      >   owner = "NixOS";
      >   repo = "nixpkgs";
      >   rev = "ae2e6b3958682513d28f7d633734571fb18285dd";
      > }
      > ```
      >
      > This evaluates to the following attribute set:
      >
      > ```nix
      > {
      >   lastModified = 1686503798;
      >   lastModifiedDate = "20230611171638";
      >   narHash = "sha256-rA9RqKP9OlBrgGCPvfd5HVAXDOy8k2SmPtB/ijShNXc=";
      >   outPath = "/nix/store/l5m6qlvfs9sdw14ja3qbzpglcjlb6j1x-source";
      >   rev = "ae2e6b3958682513d28f7d633734571fb18285dd";
      >   shortRev = "ae2e6b3";
      > }
      > ```

      > **Example**
      >
      > Fetch the same GitHub repository using the URL-like syntax:
      >
      >   ```nix
      >   builtins.fetchTree "github:NixOS/nixpkgs/ae2e6b3958682513d28f7d633734571fb18285dd"
      >   ```
    )",
    .fun = prim_fetchTree,
    .experimentalFeature = Xp::FetchTree,
});

void prim_fetchFinalTree(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    fetchTree(state, pos, args, v, {.isFinal = true});
}

static RegisterPrimOp primop_fetchFinalTree({
    .name = "fetchFinalTree",
    .args = {"input"},
    .fun = prim_fetchFinalTree,
    .internal = true,
});

static void fetch(
    EvalState & state,
    const PosIdx pos,
    Value ** args,
    Value & v,
    const std::string & who,
    bool unpack,
    std::string name)
{
    std::optional<std::string> url;
    std::optional<Hash> expectedHash;

    state.forceValue(*args[0], pos);

    bool isArgAttrs = args[0]->type() == nAttrs;
    bool nameAttrPassed = false;

    if (isArgAttrs) {

        for (auto & attr : *args[0]->attrs()) {
            std::string_view n(state.symbols[attr.name]);
            if (n == "url")
                url = state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the url we should fetch");
            else if (n == "sha256")
                expectedHash = newHashAllowEmpty(
                    state.forceStringNoCtx(
                        *attr.value, attr.pos, "while evaluating the sha256 of the content we should fetch"),
                    HashAlgorithm::SHA256);
            else if (n == "name") {
                nameAttrPassed = true;
                name = state.forceStringNoCtx(
                    *attr.value, attr.pos, "while evaluating the name of the content we should fetch");
            } else
                state.error<EvalError>("unsupported argument '%s' to '%s'", n, who).atPos(pos).debugThrow();
        }

        if (!url)
            state.error<EvalError>("'url' argument required").atPos(pos).debugThrow();
    } else
        url = state.forceStringNoCtx(*args[0], pos, "while evaluating the url we should fetch");

    if (who == "fetchTarball")
        url = state.settings.resolvePseudoUrl(*url);

    state.checkURI(*url);

    if (name == "")
        name = baseNameOf(*url);

    try {
        checkName(name);
    } catch (BadStorePathName & e) {
        auto resolution =
            nameAttrPassed
                ? HintFmt(
                      "Please change the value for the 'name' attribute passed to '%s', so that it can create a valid store path.",
                      who)
            : isArgAttrs
                ? HintFmt(
                      "Please add a valid 'name' attribute to the argument for '%s', so that it can create a valid store path.",
                      who)
                : HintFmt(
                      "Please pass an attribute set with 'url' and 'name' attributes to '%s',  so that it can create a valid store path.",
                      who);

        state
            .error<EvalError>(
                std::string("invalid store path name when fetching URL '%s': %s. %s"),
                *url,
                Uncolored(e.message()),
                Uncolored(resolution.str()))
            .atPos(pos)
            .debugThrow();
    }

    if (state.settings.pureEval && !expectedHash)
        state.error<EvalError>("in pure evaluation mode, '%s' requires a 'sha256' argument", who)
            .atPos(pos)
            .debugThrow();

    // early exit if pinned and already in the store
    if (expectedHash && expectedHash->algo == HashAlgorithm::SHA256) {
        auto expectedPath = state.store->makeFixedOutputPath(
            name,
            FixedOutputInfo{
                .method = unpack ? FileIngestionMethod::NixArchive : FileIngestionMethod::Flat,
                .hash = *expectedHash,
                .references = {}});

        // Try to get the path from the local store or substituters
        try {
            state.store->ensurePath(expectedPath);
            debug("using substituted/cached path '%s' for '%s'", state.store->printStorePath(expectedPath), *url);
            state.allowAndSetStorePathString(expectedPath, v);
            return;
        } catch (Error & e) {
            debug(
                "substitution of '%s' failed, will try to download: %s",
                state.store->printStorePath(expectedPath),
                e.what());
            // Fall through to download
        }
    }

    // Download the file/tarball if substitution failed or no hash was provided
    auto storePath = unpack ? fetchToStore(
                                  state.fetchSettings,
                                  *state.store,
                                  fetchers::downloadTarball(state.store, state.fetchSettings, *url),
                                  FetchMode::Copy,
                                  name)
                            : fetchers::downloadFile(state.store, state.fetchSettings, *url, name).storePath;

    if (expectedHash) {
        auto hash = unpack ? state.store->queryPathInfo(storePath)->narHash
                           : hashFile(HashAlgorithm::SHA256, state.store->toRealPath(storePath));
        if (hash != *expectedHash) {
            state
                .error<EvalError>(
                    "hash mismatch in file downloaded from '%s':\n  specified: %s\n  got:       %s",
                    *url,
                    expectedHash->to_string(HashFormat::Nix32, true),
                    hash.to_string(HashFormat::Nix32, true))
                .withExitStatus(102)
                .debugThrow();
        }
    }

    state.allowAndSetStorePathString(storePath, v);
}

static void prim_fetchurl(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    fetch(state, pos, args, v, "fetchurl", false, "");
}

static RegisterPrimOp primop_fetchurl({
    .name = "__fetchurl",
    .args = {"arg"},
    .doc = R"(
      Download the specified URL and return the path of the downloaded file.
      `arg` can be either a string denoting the URL, or an attribute set with the following attributes:

      - `url`

        The URL of the file to download.

      - `name` (default: the last path component of the URL)

        A name for the file in the store. This can be useful if the URL has any
        characters that are invalid for the store.

      Not available in [restricted evaluation mode](@docroot@/command-ref/conf-file.md#conf-restrict-eval).
    )",
    .fun = prim_fetchurl,
});

static void prim_fetchTarball(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    fetch(state, pos, args, v, "fetchTarball", true, "source");
}

static RegisterPrimOp primop_fetchTarball({
    .name = "fetchTarball",
    .args = {"args"},
    .doc = R"(
      Download the specified URL, unpack it and return the path of the
      unpacked tree. The file must be a tape archive (`.tar`) compressed
      with `gzip`, `bzip2` or `xz`. If the tarball consists of a
      single directory, then the top-level path component of the files
      in the tarball is removed. The typical use of the function is to
      obtain external Nix expression dependencies, such as a
      particular version of Nixpkgs, e.g.

      ```nix
      with import (fetchTarball https://github.com/NixOS/nixpkgs/archive/nixos-14.12.tar.gz) {};

      stdenv.mkDerivation { … }
      ```

      The fetched tarball is cached for a certain amount of time (1
      hour by default) in `~/.cache/nix/tarballs/`. You can change the
      cache timeout either on the command line with `--tarball-ttl`
      *number-of-seconds* or in the Nix configuration file by adding
      the line `tarball-ttl = ` *number-of-seconds*.

      Note that when obtaining the hash with `nix-prefetch-url` the
      option `--unpack` is required.

      This function can also verify the contents against a hash. In that
      case, the function takes a set instead of a URL. The set requires
      the attribute `url` and the attribute `sha256`, e.g.

      ```nix
      with import (fetchTarball {
        url = "https://github.com/NixOS/nixpkgs/archive/nixos-14.12.tar.gz";
        sha256 = "1jppksrfvbk5ypiqdz4cddxdl8z6zyzdb2srq8fcffr327ld5jj2";
      }) {};

      stdenv.mkDerivation { … }
      ```

      Not available in [restricted evaluation mode](@docroot@/command-ref/conf-file.md#conf-restrict-eval).
    )",
    .fun = prim_fetchTarball,
});

static void prim_fetchGit(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    fetchTree(
        state, pos, args, v, FetchTreeParams{.emptyRevFallback = true, .allowNameArgument = true, .isFetchGit = true});
}

static RegisterPrimOp primop_fetchGit({
    .name = "fetchGit",
    .args = {"args"},
    .doc = R"(
      Fetch a path from git. *args* can be a URL, in which case the HEAD
      of the repo at that URL is fetched. Otherwise, it can be an
      attribute with the following attributes (all except `url` optional):

      - `url`

        The URL of the repo.

      - `name` (default: `source`)

        The name of the directory the repo should be exported to in the store.

      - `rev` (default: *the tip of `ref`*)

        The [Git revision] to fetch.
        This is typically a commit hash.

        [Git revision]: https://git-scm.com/docs/git-rev-parse#_specifying_revisions

      - `ref` (default: `HEAD`)

        The [Git reference] under which to look for the requested revision.
        This is often a branch or tag name.

        [Git reference]: https://git-scm.com/book/en/v2/Git-Internals-Git-References

        This option has no effect once `shallow` cloning is enabled.

        By default, the `ref` value is prefixed with `refs/heads/`.
        As of 2.3.0, Nix doesn't prefix `refs/heads/` if `ref` starts with `refs/`.

      - `submodules` (default: `false`)

        A Boolean parameter that specifies whether submodules should be checked out.

      - `exportIgnore` (default: `true`)

        A Boolean parameter that specifies whether `export-ignore` from `.gitattributes` should be applied.
        This approximates part of the `git archive` behavior.

        Enabling this option is not recommended because it is unknown whether the Git developers commit to the reproducibility of `export-ignore` in newer Git versions.

      - `shallow` (default: `false`)

        Make a shallow clone when fetching the Git tree.
        When this is enabled, the options `ref` and `allRefs` have no effect anymore.

      - `lfs` (default: `false`)

        A boolean that when `true` specifies that [Git LFS] files should be fetched.

        [Git LFS]: https://git-lfs.com/

      - `allRefs`

        Whether to fetch all references (eg. branches and tags) of the repository.
        With this argument being true, it's possible to load a `rev` from *any* `ref`.
        (by default only `rev`s from the specified `ref` are supported).

        This option has no effect once `shallow` cloning is enabled.

      - `verifyCommit` (default: `true` if `publicKey` or `publicKeys` are provided, otherwise `false`)

        Whether to check `rev` for a signature matching `publicKey` or `publicKeys`.
        If `verifyCommit` is enabled, then `fetchGit` cannot use a local repository with uncommitted changes.
        Requires the [`verified-fetches` experimental feature](@docroot@/development/experimental-features.md#xp-feature-verified-fetches).

      - `publicKey`

        The public key against which `rev` is verified if `verifyCommit` is enabled.
        Requires the [`verified-fetches` experimental feature](@docroot@/development/experimental-features.md#xp-feature-verified-fetches).

      - `keytype` (default: `"ssh-ed25519"`)

        The key type of `publicKey`.
        Possible values:
        - `"ssh-dsa"`
        - `"ssh-ecdsa"`
        - `"ssh-ecdsa-sk"`
        - `"ssh-ed25519"`
        - `"ssh-ed25519-sk"`
        - `"ssh-rsa"`
        Requires the [`verified-fetches` experimental feature](@docroot@/development/experimental-features.md#xp-feature-verified-fetches).

      - `publicKeys`

        The public keys against which `rev` is verified if `verifyCommit` is enabled.
        Must be given as a list of attribute sets with the following form:

        ```nix
        {
          key = "<public key>";
          type = "<key type>"; # optional, default: "ssh-ed25519"
        }
        ```

        Requires the [`verified-fetches` experimental feature](@docroot@/development/experimental-features.md#xp-feature-verified-fetches).


      Here are some examples of how to use `fetchGit`.

        - To fetch a private repository over SSH:

          ```nix
          builtins.fetchGit {
            url = "git@github.com:my-secret/repository.git";
            ref = "master";
            rev = "adab8b916a45068c044658c4158d81878f9ed1c3";
          }
          ```

        - To fetch an arbitrary reference:

          ```nix
          builtins.fetchGit {
            url = "https://github.com/NixOS/nix.git";
            ref = "refs/heads/0.5-release";
          }
          ```

        - If the revision you're looking for is in the default branch of
          the git repository you don't strictly need to specify the branch
          name in the `ref` attribute.

          However, if the revision you're looking for is in a future
          branch for the non-default branch you will need to specify the
          the `ref` attribute as well.

          ```nix
          builtins.fetchGit {
            url = "https://github.com/nixos/nix.git";
            rev = "841fcbd04755c7a2865c51c1e2d3b045976b7452";
            ref = "1.11-maintenance";
          }
          ```

          > **Note**
          >
          > It is nice to always specify the branch which a revision
          > belongs to. Without the branch being specified, the fetcher
          > might fail if the default branch changes. Additionally, it can
          > be confusing to try a commit from a non-default branch and see
          > the fetch fail. If the branch is specified the fault is much
          > more obvious.

        - If the revision you're looking for is in the default branch of
          the git repository you may omit the `ref` attribute.

          ```nix
          builtins.fetchGit {
            url = "https://github.com/nixos/nix.git";
            rev = "841fcbd04755c7a2865c51c1e2d3b045976b7452";
          }
          ```

        - To fetch a specific tag:

          ```nix
          builtins.fetchGit {
            url = "https://github.com/nixos/nix.git";
            ref = "refs/tags/1.9";
          }
          ```

        - To fetch the latest version of a remote branch:

          ```nix
          builtins.fetchGit {
            url = "ssh://git@github.com/nixos/nix.git";
            ref = "master";
          }
          ```

        - To verify the commit signature:

          ```nix
          builtins.fetchGit {
            url = "ssh://git@github.com/nixos/nix.git";
            verifyCommit = true;
            publicKeys = [
                {
                  type = "ssh-ed25519";
                  key = "AAAAC3NzaC1lZDI1NTE5AAAAIArPKULJOid8eS6XETwUjO48/HKBWl7FTCK0Z//fplDi";
                }
            ];
          }
          ```

          Nix refetches the branch according to the [`tarball-ttl`](@docroot@/command-ref/conf-file.md#conf-tarball-ttl) setting.

          This behavior is disabled in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval).

        - To fetch the content of a checked-out work directory:

          ```nix
          builtins.fetchGit ./work-dir
          ```

      If the URL points to a local directory, and no `ref` or `rev` is
      given, `fetchGit` uses the current content of the checked-out
      files, even if they are not committed or added to Git's index. It
      only considers files added to the Git repository, as listed by `git ls-files`.
    )",
    .fun = prim_fetchGit,
});

} // namespace nix
