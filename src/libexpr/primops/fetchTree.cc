#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "filetransfer.hh"
#include "registry.hh"

#include <ctime>
#include <iomanip>
#include <regex>

namespace nix {

void emitTreeAttrs(
    EvalState & state,
    const fetchers::Tree & tree,
    const fetchers::Input & input,
    Value & v,
    bool emptyRevFallback,
    bool forceDirty)
{
    assert(input.isLocked());

    auto attrs = state.buildBindings(8);

    auto storePath = state.store->printStorePath(tree.storePath);

    attrs.alloc(state.sOutPath).mkString(storePath, {storePath});

    // FIXME: support arbitrary input attributes.

    auto narHash = input.getNarHash();
    assert(narHash);
    attrs.alloc("narHash").mkString(narHash->to_string(SRI, true));

    if (input.getType() == "git")
        attrs.alloc("submodules").mkBool(
            fetchers::maybeGetBoolAttr(input.attrs, "submodules").value_or(false));

    if (!forceDirty) {

        if (auto rev = input.getRev()) {
            attrs.alloc("rev").mkString(rev->gitRev());
            attrs.alloc("shortRev").mkString(rev->gitShortRev());
        } else if (emptyRevFallback) {
            // Backwards compat for `builtins.fetchGit`: dirty repos return an empty sha1 as rev
            auto emptyHash = Hash(htSHA1);
            attrs.alloc("rev").mkString(emptyHash.gitRev());
            attrs.alloc("shortRev").mkString(emptyHash.gitShortRev());
        }

        if (auto revCount = input.getRevCount())
            attrs.alloc("revCount").mkInt(*revCount);
        else if (emptyRevFallback)
            attrs.alloc("revCount").mkInt(0);

    }

    if (auto lastModified = input.getLastModified()) {
        attrs.alloc("lastModified").mkInt(*lastModified);
        attrs.alloc("lastModifiedDate").mkString(
            fmt("%s", std::put_time(std::gmtime(&*lastModified), "%Y%m%d%H%M%S")));
    }

    v.mkAttrs(attrs);
}

std::string fixURI(std::string uri, EvalState & state, const std::string & defaultScheme = "file")
{
    state.checkURI(uri);
    return uri.find("://") != std::string::npos ? uri : defaultScheme + "://" + uri;
}

std::string fixURIForGit(std::string uri, EvalState & state)
{
    /* Detects scp-style uris (e.g. git@github.com:NixOS/nix) and fixes
     * them by removing the `:` and assuming a scheme of `ssh://`
     * */
    static std::regex scp_uri("([^/]*)@(.*):(.*)");
    if (uri[0] != '/' && std::regex_match(uri, scp_uri))
        return fixURI(std::regex_replace(uri, scp_uri, "$1@$2/$3"), state, "ssh");
    else
        return fixURI(uri, state);
}

struct FetchTreeParams {
    bool emptyRevFallback = false;
    bool allowNameArgument = false;
};

static void fetchTree(
    EvalState & state,
    const Pos & pos,
    Value * * args,
    Value & v,
    std::optional<std::string> type,
    const FetchTreeParams & params = FetchTreeParams{}
) {
    fetchers::Input input;
    PathSet context;

    state.forceValue(*args[0], pos);

    if (args[0]->type() == nAttrs) {
        state.forceAttrs(*args[0], pos);

        fetchers::Attrs attrs;

        if (auto aType = args[0]->attrs->get(state.sType)) {
            if (type)
                throw Error({
                    .msg = hintfmt("unexpected attribute 'type'"),
                    .errPos = pos
                });
            type = state.forceStringNoCtx(*aType->value, *aType->pos);
        } else if (!type)
            throw Error({
                .msg = hintfmt("attribute 'type' is missing in call to 'fetchTree'"),
                .errPos = pos
            });

        attrs.emplace("type", type.value());

        for (auto & attr : *args[0]->attrs) {
            if (attr.name == state.sType) continue;
            state.forceValue(*attr.value, *attr.pos);
            if (attr.value->type() == nPath || attr.value->type() == nString) {
                auto s = state.coerceToString(*attr.pos, *attr.value, context, false, false).toOwned();
                attrs.emplace(attr.name,
                    attr.name == "url"
                    ? type == "git"
                      ? fixURIForGit(s, state)
                      : fixURI(s, state)
                    : s);
            }
            else if (attr.value->type() == nBool)
                attrs.emplace(attr.name, Explicit<bool>{attr.value->boolean});
            else if (attr.value->type() == nInt)
                attrs.emplace(attr.name, uint64_t(attr.value->integer));
            else
                throw TypeError("fetchTree argument '%s' is %s while a string, Boolean or integer is expected",
                    attr.name, showType(*attr.value));
        }

        if (!params.allowNameArgument)
            if (auto nameIter = attrs.find("name"); nameIter != attrs.end())
                throw Error({
                    .msg = hintfmt("attribute 'name' isn't supported in call to 'fetchTree'"),
                    .errPos = pos
                });

        input = fetchers::Input::fromAttrs(std::move(attrs));
    } else {
        auto url = state.coerceToString(pos, *args[0], context, false, false).toOwned();

        if (type == "git") {
            fetchers::Attrs attrs;
            attrs.emplace("type", "git");
            attrs.emplace("url", fixURIForGit(url, state));
            input = fetchers::Input::fromAttrs(std::move(attrs));
        } else {
            input = fetchers::Input::fromURL(fixURI(url, state));
        }
    }

    if (!evalSettings.pureEval && !input.isDirect())
        input = lookupInRegistries(state.store, input).first;

    if (evalSettings.pureEval && !input.isLocked())
        throw Error("in pure evaluation mode, 'fetchTree' requires a locked input, at %s", pos);

    auto [tree, input2] = input.fetch(state.store);

    state.allowPath(tree.storePath);

    emitTreeAttrs(state, tree, input2, v, params.emptyRevFallback, false);
}

static void prim_fetchTree(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    settings.requireExperimentalFeature(Xp::Flakes);
    fetchTree(state, pos, args, v, std::nullopt, FetchTreeParams { .allowNameArgument = false });
}

// FIXME: document
static RegisterPrimOp primop_fetchTree("fetchTree", 1, prim_fetchTree);

static void fetch(EvalState & state, const Pos & pos, Value * * args, Value & v,
    const std::string & who, bool unpack, std::string name)
{
    std::optional<std::string> url;
    std::optional<Hash> expectedHash;

    state.forceValue(*args[0], pos);

    if (args[0]->type() == nAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            std::string n(attr.name);
            if (n == "url")
                url = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "sha256")
                expectedHash = newHashAllowEmpty(state.forceStringNoCtx(*attr.value, *attr.pos), htSHA256);
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError({
                    .msg = hintfmt("unsupported argument '%s' to '%s'", attr.name, who),
                    .errPos = *attr.pos
                });
            }

        if (!url)
            throw EvalError({
                .msg = hintfmt("'url' argument required"),
                .errPos = pos
            });
    } else
        url = state.forceStringNoCtx(*args[0], pos);

    url = resolveUri(*url);

    state.checkURI(*url);

    if (name == "")
        name = baseNameOf(*url);

    if (evalSettings.pureEval && !expectedHash)
        throw Error("in pure evaluation mode, '%s' requires a 'sha256' argument", who);

    // early exit if pinned and already in the store
    if (expectedHash && expectedHash->type == htSHA256) {
        auto expectedPath =
            unpack
            ? state.store->makeFixedOutputPath(FileIngestionMethod::Recursive, *expectedHash, name, {})
            : state.store->makeFixedOutputPath(FileIngestionMethod::Flat, *expectedHash, name, {});

        if (state.store->isValidPath(expectedPath)) {
            state.allowAndSetStorePathString(expectedPath, v);
            return;
        }
    }

    // TODO: fetching may fail, yet the path may be substitutable.
    //       https://github.com/NixOS/nix/issues/4313
    auto storePath =
        unpack
        ? fetchers::downloadTarball(state.store, *url, name, (bool) expectedHash).first.storePath
        : fetchers::downloadFile(state.store, *url, name, (bool) expectedHash).storePath;

    if (expectedHash) {
        auto hash = unpack
            ? state.store->queryPathInfo(storePath)->narHash
            : hashFile(htSHA256, state.store->toRealPath(storePath));
        if (hash != *expectedHash)
            throw Error((unsigned int) 102, "hash mismatch in file downloaded from '%s':\n  specified: %s\n  got:       %s",
                *url, expectedHash->to_string(Base32, true), hash.to_string(Base32, true));
    }

    state.allowAndSetStorePathString(storePath, v);
}

static void prim_fetchurl(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    fetch(state, pos, args, v, "fetchurl", false, "");
}

static RegisterPrimOp primop_fetchurl({
    .name = "__fetchurl",
    .args = {"url"},
    .doc = R"(
      Download the specified URL and return the path of the downloaded
      file. This function is not available if [restricted evaluation
      mode](../command-ref/conf-file.md) is enabled.
    )",
    .fun = prim_fetchurl,
});

static void prim_fetchTarball(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    fetch(state, pos, args, v, "fetchTarball", true, "source");
}

static RegisterPrimOp primop_fetchTarball({
    .name = "fetchTarball",
    .args = {"args"},
    .doc = R"(
      Download the specified URL, unpack it and return the path of the
      unpacked tree. The file must be a tape archive (`.tar`) compressed
      with `gzip`, `bzip2` or `xz`. The top-level path component of the
      files in the tarball is removed, so it is best if the tarball
      contains a single directory at top level. The typical use of the
      function is to obtain external Nix expression dependencies, such as
      a particular version of Nixpkgs, e.g.

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

      This function is not available if [restricted evaluation
      mode](../command-ref/conf-file.md) is enabled.
    )",
    .fun = prim_fetchTarball,
});

static void prim_fetchGit(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    fetchTree(state, pos, args, v, "git", FetchTreeParams { .emptyRevFallback = true, .allowNameArgument = true });
}

static RegisterPrimOp primop_fetchGit({
    .name = "fetchGit",
    .args = {"args"},
    .doc = R"(
      Fetch a path from git. *args* can be a URL, in which case the HEAD
      of the repo at that URL is fetched. Otherwise, it can be an
      attribute with the following attributes (all except `url` optional):

        - url\
          The URL of the repo.

        - name\
          The name of the directory the repo should be exported to in the
          store. Defaults to the basename of the URL.

        - rev\
          The git revision to fetch. Defaults to the tip of `ref`.

        - ref\
          The git ref to look for the requested revision under. This is
          often a branch or tag name. Defaults to `HEAD`.

          By default, the `ref` value is prefixed with `refs/heads/`. As
          of Nix 2.3.0 Nix will not prefix `refs/heads/` if `ref` starts
          with `refs/`.

        - submodules\
          A Boolean parameter that specifies whether submodules should be
          checked out. Defaults to `false`.

        - allRefs\
          Whether to fetch all refs of the repository. With this argument being
          true, it's possible to load a `rev` from *any* `ref` (by default only
          `rev`s from the specified `ref` are supported).

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

          > **Note**
          >
          > Nix will refetch the branch in accordance with
          > the option `tarball-ttl`.

          > **Note**
          >
          > This behavior is disabled in *Pure evaluation mode*.
    )",
    .fun = prim_fetchGit,
});

}
