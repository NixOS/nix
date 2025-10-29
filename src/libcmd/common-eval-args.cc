#include "nix/fetchers/fetch-settings.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/cmd/common-eval-args.hh"
#include "nix/main/shared.hh"
#include "nix/util/config-global.hh"
#include "nix/store/filetransfer.hh"
#include "nix/expr/eval.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/registry.hh"
#include "nix/flake/flakeref.hh"
#include "nix/flake/settings.hh"
#include "nix/store/store-open.hh"
#include "nix/cmd/command.hh"
#include "nix/fetchers/tarball.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/cmd/compatibility-settings.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/globals.hh"

namespace nix {

fetchers::Settings fetchSettings;

static GlobalConfig::Register rFetchSettings(&fetchSettings);

EvalSettings evalSettings{
    settings.readOnlyMode,
    {
        {
            "flake",
            [](EvalState & state, std::string_view rest) {
                experimentalFeatureSettings.require(Xp::Flakes);
                // FIXME `parseFlakeRef` should take a `std::string_view`.
                auto flakeRef = parseFlakeRef(fetchSettings, std::string{rest}, {}, true, false);
                debug("fetching flake search path element '%s''", rest);
                auto [accessor, lockedRef] = flakeRef.resolve(state.store).lazyFetch(state.store);
                auto storePath = nix::fetchToStore(
                    state.fetchSettings,
                    *state.store,
                    SourcePath(accessor),
                    FetchMode::Copy,
                    lockedRef.input.getName());
                state.allowPath(storePath);
                return state.storePath(storePath);
            },
        },
    },
};

static GlobalConfig::Register rEvalSettings(&evalSettings);

flake::Settings flakeSettings;

static GlobalConfig::Register rFlakeSettings(&flakeSettings);

CompatibilitySettings compatibilitySettings{};

static GlobalConfig::Register rCompatibilitySettings(&compatibilitySettings);

MixEvalArgs::MixEvalArgs()
{
    addFlag({
        .longName = "arg",
        .description = "Pass the value *expr* as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name", "expr"},
        .handler = {[&](std::string name, std::string expr) {
            autoArgs.insert_or_assign(name, AutoArg{AutoArgExpr{expr}});
        }},
    });

    addFlag({
        .longName = "argstr",
        .description = "Pass the string *string* as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name", "string"},
        .handler = {[&](std::string name, std::string s) {
            autoArgs.insert_or_assign(name, AutoArg{AutoArgString{s}});
        }},
    });

    addFlag({
        .longName = "arg-from-file",
        .description = "Pass the contents of file *path* as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name", "path"},
        .handler = {[&](std::string name, std::string path) {
            autoArgs.insert_or_assign(name, AutoArg{AutoArgFile{path}});
        }},
        .completer = completePath,
    });

    addFlag({
        .longName = "arg-from-stdin",
        .description = "Pass the contents of stdin as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name"},
        .handler = {[&](std::string name) { autoArgs.insert_or_assign(name, AutoArg{AutoArgStdin{}}); }},
    });

    addFlag({
        .longName = "include",
        .shortName = 'I',
        .description = R"(
  Add *path* to search path entries used to resolve [lookup paths](@docroot@/language/constructs/lookup-path.md)

  This option may be given multiple times.

  Paths added through `-I` take precedence over the [`nix-path` configuration setting](@docroot@/command-ref/conf-file.md#conf-nix-path) and the [`NIX_PATH` environment variable](@docroot@/command-ref/env-common.md#env-NIX_PATH).
  )",
        .category = category,
        .labels = {"path"},
        .handler = {[&](std::string s) { lookupPath.elements.emplace_back(LookupPath::Elem::parse(s)); }},
    });

    addFlag({
        .longName = "impure",
        .description = "Allow access to mutable paths and repositories.",
        .category = category,
        .handler = {[&]() { evalSettings.pureEval = false; }},
    });

    addFlag({
        .longName = "override-flake",
        .description = "Override the flake registries, redirecting *original-ref* to *resolved-ref*.",
        .category = category,
        .labels = {"original-ref", "resolved-ref"},
        .handler = {[&](std::string _from, std::string _to) {
            auto from = parseFlakeRef(fetchSettings, _from, std::filesystem::current_path().string());
            auto to = parseFlakeRef(fetchSettings, _to, std::filesystem::current_path().string());
            fetchers::Attrs extraAttrs;
            if (to.subdir != "")
                extraAttrs["dir"] = to.subdir;
            fetchers::overrideRegistry(from.input, to.input, extraAttrs);
        }},
        .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
            completeFlakeRef(completions, openStore(), prefix);
        }},
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
    for (auto & [name, arg] : autoArgs) {
        auto v = state.allocValue();
        std::visit(
            overloaded{
                [&](const AutoArgExpr & arg) {
                    state.mkThunk_(
                        *v,
                        state.parseExprFromString(
                            arg.expr,
                            compatibilitySettings.nixShellShebangArgumentsRelativeToScript
                                ? state.rootPath(absPath(getCommandBaseDir()))
                                : state.rootPath(".")));
                },
                [&](const AutoArgString & arg) { v->mkString(arg.s); },
                [&](const AutoArgFile & arg) { v->mkString(readFile(arg.path.string())); },
                [&](const AutoArgStdin & arg) { v->mkString(readFile(STDIN_FILENO)); }},
            arg);
        res.insert(state.symbols.create(name), v);
    }
    return res.finish();
}

SourcePath lookupFileArg(EvalState & state, std::string_view s, const Path * baseDir)
{
    if (EvalSettings::isPseudoUrl(s)) {
        auto accessor = fetchers::downloadTarball(state.store, state.fetchSettings, EvalSettings::resolvePseudoUrl(s));
        auto storePath = fetchToStore(state.fetchSettings, *state.store, SourcePath(accessor), FetchMode::Copy);
        return state.storePath(storePath);
    }

    else if (hasPrefix(s, "flake:")) {
        experimentalFeatureSettings.require(Xp::Flakes);
        auto flakeRef = parseFlakeRef(fetchSettings, std::string(s.substr(6)), {}, true, false);
        auto [accessor, lockedRef] = flakeRef.resolve(state.store).lazyFetch(state.store);
        auto storePath = nix::fetchToStore(
            state.fetchSettings, *state.store, SourcePath(accessor), FetchMode::Copy, lockedRef.input.getName());
        state.allowPath(storePath);
        return state.storePath(storePath);
    }

    else if (s.size() > 2 && s.at(0) == '<' && s.at(s.size() - 1) == '>') {
        Path p(s.substr(1, s.size() - 2));
        return state.findFile(p);
    }

    else
        return state.rootPath(baseDir ? absPath(s, *baseDir) : absPath(s));
}

} // namespace nix
