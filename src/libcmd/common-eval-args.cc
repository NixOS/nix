#include "eval-settings.hh"
#include "common-eval-args.hh"
#include "shared.hh"
#include "filetransfer.hh"
#include "eval.hh"
#include "fetchers.hh"
#include "registry.hh"
#include "flake/flakeref.hh"
#include "store-api.hh"
#include "command.hh"
#include "tarball.hh"

namespace nix {

MixEvalArgs::MixEvalArgs()
{
    addFlag({
        .longName = "arg",
        .description = "Pass the value *expr* as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name", "expr"},
        .handler = {[&](std::string name, std::string expr) { autoArgs[name] = 'E' + expr; }}
    });

    addFlag({
        .longName = "argstr",
        .description = "Pass the string *string* as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name", "string"},
        .handler = {[&](std::string name, std::string s) { autoArgs[name] = 'S' + s; }},
    });

    addFlag({
        .longName = "include",
        .shortName = 'I',
        .description = R"(
  Add *path* to the Nix search path. The Nix search path is
  initialized from the colon-separated [`NIX_PATH`](@docroot@/command-ref/env-common.md#env-NIX_PATH) environment
  variable, and is used to look up the location of Nix expressions using [paths](@docroot@/language/values.md#type-path) enclosed in angle
  brackets (i.e., `<nixpkgs>`).

  For instance, passing

  ```
  -I /home/eelco/Dev
  -I /etc/nixos
  ```

  will cause Nix to look for paths relative to `/home/eelco/Dev` and
  `/etc/nixos`, in that order. This is equivalent to setting the
  `NIX_PATH` environment variable to

  ```
  /home/eelco/Dev:/etc/nixos
  ```

  It is also possible to match paths against a prefix. For example,
  passing

  ```
  -I nixpkgs=/home/eelco/Dev/nixpkgs-branch
  -I /etc/nixos
  ```

  will cause Nix to search for `<nixpkgs/path>` in
  `/home/eelco/Dev/nixpkgs-branch/path` and `/etc/nixos/nixpkgs/path`.

  If a path in the Nix search path starts with `http://` or `https://`,
  it is interpreted as the URL of a tarball that will be downloaded and
  unpacked to a temporary location. The tarball must consist of a single
  top-level directory. For example, passing

  ```
  -I nixpkgs=https://github.com/NixOS/nixpkgs/archive/master.tar.gz
  ```

  tells Nix to download and use the current contents of the `master`
  branch in the `nixpkgs` repository.

  The URLs of the tarballs from the official `nixos.org` channels
  (see [the manual page for `nix-channel`](../nix-channel.md)) can be
  abbreviated as `channel:<channel-name>`.  For instance, the
  following two flags are equivalent:

  ```
  -I nixpkgs=channel:nixos-21.05
  -I nixpkgs=https://nixos.org/channels/nixos-21.05/nixexprs.tar.xz
  ```

  You can also fetch source trees using [flake URLs](./nix3-flake.md#url-like-syntax) and add them to the
  search path. For instance,

  ```
  -I nixpkgs=flake:nixpkgs
  ```

  specifies that the prefix `nixpkgs` shall refer to the source tree
  downloaded from the `nixpkgs` entry in the flake registry. Similarly,

  ```
  -I nixpkgs=flake:github:NixOS/nixpkgs/nixos-22.05
  ```

  makes `<nixpkgs>` refer to a particular branch of the
  `NixOS/nixpkgs` repository on GitHub.
  )",
        .category = category,
        .labels = {"path"},
        .handler = {[&](std::string s) {
            searchPath.elements.emplace_back(SearchPath::Elem::parse(s));
        }}
    });

    addFlag({
        .longName = "impure",
        .description = "Allow access to mutable paths and repositories.",
        .category = category,
        .handler = {[&]() {
            evalSettings.pureEval = false;
        }},
    });

    addFlag({
        .longName = "override-flake",
        .description = "Override the flake registries, redirecting *original-ref* to *resolved-ref*.",
        .category = category,
        .labels = {"original-ref", "resolved-ref"},
        .handler = {[&](std::string _from, std::string _to) {
            auto from = parseFlakeRef(_from, absPath("."));
            auto to = parseFlakeRef(_to, absPath("."));
            fetchers::Attrs extraAttrs;
            if (to.subdir != "") extraAttrs["dir"] = to.subdir;
            fetchers::overrideRegistry(from.input, to.input, extraAttrs);
        }},
        .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
            completeFlakeRef(completions, openStore(), prefix);
        }}
    });

    addFlag({
        .longName = "eval-store",
        .description =
          R"(
            The [URL of the Nix store](@docroot@/store/types/index.md#store-url-format)
            to use for evaluation, i.e. to store derivations (`.drv` files) and inputs referenced by them.
          )",
        .category = category,
        .labels = {"store-url"},
        .handler = {&evalStoreUrl},
    });
}

Bindings * MixEvalArgs::getAutoArgs(EvalState & state)
{
    auto res = state.buildBindings(autoArgs.size());
    for (auto & i : autoArgs) {
        auto v = state.allocValue();
        if (i.second[0] == 'E')
            state.mkThunk_(*v, state.parseExprFromString(i.second.substr(1), state.rootPath(CanonPath::fromCwd())));
        else
            v->mkString(((std::string_view) i.second).substr(1));
        res.insert(state.symbols.create(i.first), v);
    }
    return res.finish();
}

SourcePath lookupFileArg(EvalState & state, std::string_view s, CanonPath baseDir)
{
    if (EvalSettings::isPseudoUrl(s)) {
        auto storePath = fetchers::downloadTarball(
            state.store, EvalSettings::resolvePseudoUrl(s), "source", false).storePath;
        return state.rootPath(CanonPath(state.store->toRealPath(storePath)));
    }

    else if (hasPrefix(s, "flake:")) {
        experimentalFeatureSettings.require(Xp::Flakes);
        auto flakeRef = parseFlakeRef(std::string(s.substr(6)), {}, true, false);
        auto storePath = flakeRef.resolve(state.store).fetchTree(state.store).first;
        return state.rootPath(CanonPath(state.store->toRealPath(storePath)));
    }

    else if (s.size() > 2 && s.at(0) == '<' && s.at(s.size() - 1) == '>') {
        Path p(s.substr(1, s.size() - 2));
        return state.findFile(p);
    }

    else
        return state.rootPath(CanonPath(s, baseDir));
}

}
