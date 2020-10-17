#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "filetransfer.hh"
#include "registry.hh"

#include <ctime>
#include <iomanip>

namespace nix {

void emitTreeAttrs(
    EvalState & state,
    const fetchers::Tree & tree,
    const fetchers::Input & input,
    Value & v,
    bool emptyRevFallback)
{
    assert(input.isImmutable());

    state.mkAttrs(v, 8);

    auto storePath = state.store->printStorePath(tree.storePath);

    mkString(*state.allocAttr(v, state.sOutPath), storePath, PathSet({storePath}));

    // FIXME: support arbitrary input attributes.

    auto narHash = input.getNarHash();
    assert(narHash);
    mkString(*state.allocAttr(v, state.symbols.create("narHash")),
        narHash->to_string(SRI, true));

    if (auto rev = input.getRev()) {
        mkString(*state.allocAttr(v, state.symbols.create("rev")), rev->gitRev());
        mkString(*state.allocAttr(v, state.symbols.create("shortRev")), rev->gitShortRev());
    } else if (emptyRevFallback) {
        // Backwards compat for `builtins.fetchGit`: dirty repos return an empty sha1 as rev
        auto emptyHash = Hash(htSHA1);
        mkString(*state.allocAttr(v, state.symbols.create("rev")), emptyHash.gitRev());
        mkString(*state.allocAttr(v, state.symbols.create("shortRev")), emptyHash.gitRev());
    }

    if (input.getType() == "git")
        mkBool(*state.allocAttr(v, state.symbols.create("submodules")), maybeGetBoolAttr(input.attrs, "submodules").value_or(false));

    if (auto revCount = input.getRevCount())
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), *revCount);
    else if (emptyRevFallback)
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), 0);

    if (auto lastModified = input.getLastModified()) {
        mkInt(*state.allocAttr(v, state.symbols.create("lastModified")), *lastModified);
        mkString(*state.allocAttr(v, state.symbols.create("lastModifiedDate")),
            fmt("%s", std::put_time(std::gmtime(&*lastModified), "%Y%m%d%H%M%S")));
    }

    v.attrs->sort();
}

std::string fixURI(std::string uri, EvalState &state)
{
    state.checkURI(uri);
    return uri.find("://") != std::string::npos ? uri : "file://" + uri;
}

void addURI(EvalState &state, fetchers::Attrs &attrs, Symbol name, std::string v)
{
    string n(name);
    attrs.emplace(name, n == "url" ? fixURI(v, state) : v);
}

static void fetchTree(
    EvalState &state,
    const Pos &pos,
    Value **args,
    Value &v,
    const std::optional<std::string> type,
    bool emptyRevFallback = false
) {
    fetchers::Input input;
    PathSet context;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {
        state.forceAttrs(*args[0], pos);

        fetchers::Attrs attrs;

        for (auto & attr : *args[0]->attrs) {
            state.forceValue(*attr.value);
            if (attr.value->type == tPath || attr.value->type == tString)
                addURI(
                    state,
                    attrs,
                    attr.name,
                    state.coerceToString(*attr.pos, *attr.value, context, false, false)
                );
            else if (attr.value->type == tString)
                addURI(state, attrs, attr.name, attr.value->string.s);
            else if (attr.value->type == tBool)
                attrs.emplace(attr.name, fetchers::Explicit<bool>{attr.value->boolean});
            else if (attr.value->type == tInt)
                attrs.emplace(attr.name, attr.value->integer);
            else
                throw TypeError("fetchTree argument '%s' is %s while a string, Boolean or integer is expected",
                    attr.name, showType(*attr.value));
        }

        if (type)
            attrs.emplace("type", type.value());

        if (!attrs.count("type"))
            throw Error({
                .hint = hintfmt("attribute 'type' is missing in call to 'fetchTree'"),
                .errPos = pos
            });

        input = fetchers::Input::fromAttrs(std::move(attrs));
    } else {
        auto url = fixURI(state.coerceToString(pos, *args[0], context, false, false), state);

        if (type == "git") {
            fetchers::Attrs attrs;
            attrs.emplace("type", "git");
            attrs.emplace("url", url);
            input = fetchers::Input::fromAttrs(std::move(attrs));
        } else {
            input = fetchers::Input::fromURL(url);
        }
    }

    if (!evalSettings.pureEval && !input.isDirect())
        input = lookupInRegistries(state.store, input).first;

    if (evalSettings.pureEval && !input.isImmutable())
        throw Error("in pure evaluation mode, 'fetchTree' requires an immutable input, at %s", pos);

    auto [tree, input2] = input.fetch(state.store);

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);

    emitTreeAttrs(state, tree, input2, v, emptyRevFallback);
}

static void prim_fetchTree(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    settings.requireExperimentalFeature("flakes");
    fetchTree(state, pos, args, v, std::nullopt);
}

static RegisterPrimOp primop_fetchTree("fetchTree", 1, prim_fetchTree);

static void fetch(EvalState & state, const Pos & pos, Value * * args, Value & v,
    const string & who, bool unpack, std::string name)
{
    std::optional<std::string> url;
    std::optional<Hash> expectedHash;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string n(attr.name);
            if (n == "url")
                url = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "sha256")
                expectedHash = newHashAllowEmpty(state.forceStringNoCtx(*attr.value, *attr.pos), htSHA256);
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError({
                    .hint = hintfmt("unsupported argument '%s' to '%s'", attr.name, who),
                    .errPos = *attr.pos
                });
            }

        if (!url)
            throw EvalError({
                .hint = hintfmt("'url' argument required"),
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

    auto storePath =
        unpack
        ? fetchers::downloadTarball(state.store, *url, name, (bool) expectedHash).first.storePath
        : fetchers::downloadFile(state.store, *url, name, (bool) expectedHash).storePath;

    auto path = state.store->toRealPath(storePath);

    if (expectedHash) {
        auto hash = unpack
            ? state.store->queryPathInfo(storePath)->narHash
            : hashFile(htSHA256, path);
        if (hash != *expectedHash)
            throw Error((unsigned int) 102, "hash mismatch in file downloaded from '%s':\n  wanted: %s\n  got:    %s",
                *url, expectedHash->to_string(Base32, true), hash.to_string(Base32, true));
    }

    if (state.allowedPaths)
        state.allowedPaths->insert(path);

    mkString(v, path, PathSet({path}));
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

      The fetched tarball is cached for a certain amount of time (1 hour
      by default) in `~/.cache/nix/tarballs/`. You can change the cache
      timeout either on the command line with `--option tarball-ttl number
      of seconds` or in the Nix configuration file with this option: ` 
      number of seconds to cache `.

      Note that when obtaining the hash with ` nix-prefetch-url ` the
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

static void prim_fetchGit(EvalState &state, const Pos &pos, Value **args, Value &v)
{
    fetchTree(state, pos, args, v, "git", true);
}

static RegisterPrimOp primop_fetchGit({
    .name = "fetchGit",
    .args = {"args"},
    .doc = R"(
      Fetch a path from git. *args* can be a URL, in which case the HEAD
      of the repo at that URL is fetched. Otherwise, it can be an
      attribute with the following attributes (all except `url` optional):

        - url  
          The URL of the repo.

        - name  
          The name of the directory the repo should be exported to in the
          store. Defaults to the basename of the URL.

        - rev  
          The git revision to fetch. Defaults to the tip of `ref`.

        - ref  
          The git ref to look for the requested revision under. This is
          often a branch or tag name. Defaults to `HEAD`.

          By default, the `ref` value is prefixed with `refs/heads/`. As
          of Nix 2.3.0 Nix will not prefix `refs/heads/` if `ref` starts
          with `refs/`.

        - submodules  
          A Boolean parameter that specifies whether submodules should be
          checked out. Defaults to `false`.

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
