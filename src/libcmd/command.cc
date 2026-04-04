#include <algorithm>
#include <nlohmann/json.hpp>

#include "nix/cmd/command.hh"
#include "nix/cmd/legacy.hh"
#include "nix/cmd/markdown.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/derivations.hh"
#include "nix/expr/nixexpr.hh"
#include "nix/store/profiles.hh"
#include "nix/cmd/repl.hh"
#include "nix/util/strings.hh"
#include "nix/util/environment-variables.hh"

namespace nix {

RegisterCommand::Commands & RegisterCommand::commands()
{
    static RegisterCommand::Commands commands;
    return commands;
}

RegisterLegacyCommand::Commands & RegisterLegacyCommand::commands()
{
    static RegisterLegacyCommand::Commands commands;
    return commands;
}

nix::Commands RegisterCommand::getCommandsFor(const std::vector<std::string> & prefix)
{
    nix::Commands res;
    for (auto & [name, command] : RegisterCommand::commands())
        if (name.size() == prefix.size() + 1) {
            bool equal = true;
            for (size_t i = 0; i < prefix.size(); ++i)
                if (name[i] != prefix[i])
                    equal = false;
            if (equal)
                res.insert_or_assign(name[prefix.size()], command);
        }
    return res;
}

nlohmann::json NixMultiCommand::toJSON()
{
    // FIXME: use Command::toJSON() as well.
    return MultiCommand::toJSON();
}

void NixMultiCommand::run()
{
    if (!command) {
        StringSet subCommandTextLines;
        for (auto & [name, _] : commands)
            subCommandTextLines.insert(fmt("- `%s`", name));
        std::string markdownError =
            fmt("`nix %s` requires a sub-command. Available sub-commands:\n\n%s\n",
                commandName,
                concatStringsSep("\n", subCommandTextLines));
        throw UsageError(renderMarkdownToTerminal(markdownError));
    }
    command->second->run();
}

StoreConfigCommand::StoreConfigCommand() {}

ref<StoreConfig> StoreConfigCommand::getStoreConfig()
{
    if (!_storeConfig)
        _storeConfig = createStoreConfig();
    return ref<StoreConfig>(_storeConfig);
}

ref<StoreConfig> StoreConfigCommand::createStoreConfig()
{
    return resolveStoreConfig(StoreReference{settings.storeUri.get()});
}

void StoreConfigCommand::run()
{
    run(getStoreConfig());
}

StoreCommand::StoreCommand() {}

ref<Store> StoreCommand::getStore()
{
    if (!_store)
        _store = createStore();
    return ref<Store>(_store);
}

ref<Store> StoreCommand::createStore()
{
    auto store = getStoreConfig()->openStore();
    store->init();
    return store;
}

void StoreCommand::run(ref<StoreConfig> storeConfig)
{
    // We can either efficiently implement getStore/createStore with memoization,
    // or use the StoreConfig passed in run.
    // It's more efficient to memoize, especially since there are some direct users
    // of getStore. The StoreConfig in both cases should be the same, though.
    auto store = getStore();
    assert(&*storeConfig == &store->config);
    run(std::move(store));
}

CopyCommand::CopyCommand()
{
    addFlag({
        .longName = "from",
        .description = "URL of the source Nix store.",
        .labels = {"store-uri"},
        .handler = {[this](std::string s) { srcUri = StoreReference::parse(s); }},
    });

    addFlag({
        .longName = "to",
        .description = "URL of the destination Nix store.",
        .labels = {"store-uri"},
        .handler = {[this](std::string s) { dstUri = StoreReference::parse(s); }},
    });
}

ref<StoreConfig> CopyCommand::createStoreConfig()
{
    return !srcUri ? StoreCommand::createStoreConfig() : resolveStoreConfig(StoreReference{*srcUri});
}

ref<Store> CopyCommand::getDstStore()
{
    if (!srcUri && !dstUri)
        throw UsageError("you must pass '--from' and/or '--to'");

    return !dstUri ? openStore() : openStore(StoreReference{*dstUri});
}

EvalCommand::EvalCommand()
{
    addFlag({
        .longName = "debugger",
        .description = "Start an interactive environment if evaluation fails.",
        .category = MixEvalArgs::category,
        .handler = {&startReplOnEvalErrors, true},
    });
}

EvalCommand::~EvalCommand()
{
    if (evalState)
        evalState->maybePrintStats();
}

ref<Store> EvalCommand::getEvalStore()
{
    if (!evalStore)
        evalStore = evalStoreUrl ? openStore(StoreReference{*evalStoreUrl}) : getStore();
    return ref<Store>(evalStore);
}

ref<EvalState> EvalCommand::getEvalState()
{
    if (!evalState) {
        evalState = std::allocate_shared<EvalState>(
            traceable_allocator<EvalState>(), lookupPath, getEvalStore(), fetchSettings, evalSettings, getStore());

        evalState->repair = repair;

        if (startReplOnEvalErrors) {
            evalState->debugRepl = &AbstractNixRepl::runSimple;
        };
    }
    return ref<EvalState>(evalState);
}

MixOperateOnOptions::MixOperateOnOptions()
{
    addFlag({
        .longName = "derivation",
        .description =
            "Operate on the [store derivation](@docroot@/glossary.md#gloss-store-derivation) rather than its outputs.",
        .category = installablesCategory,
        .handler = {&operateOn, OperateOn::Derivation},
    });
}

BuiltPathsCommand::BuiltPathsCommand(bool recursive)
    : recursive(recursive)
{
    if (recursive)
        addFlag({
            .longName = "no-recursive",
            .description = "Apply operation to specified paths only.",
            .category = installablesCategory,
            .handler = {&this->recursive, false},
        });
    else
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "Apply operation to closure of the specified paths.",
            .category = installablesCategory,
            .handler = {&this->recursive, true},
        });

    addFlag({
        .longName = "all",
        .description = "Apply the operation to every store path.",
        .category = installablesCategory,
        .handler = {&all, true},
    });
}

void BuiltPathsCommand::run(ref<Store> store, Installables && installables)
{
    BuiltPaths rootPaths, allPaths;

    if (all) {
        if (installables.size())
            throw UsageError("'--all' does not expect arguments");
        // XXX: Only uses opaque paths, ignores all the realisations
        for (auto & p : store->queryAllValidPaths())
            rootPaths.emplace_back(BuiltPath::Opaque{p});
        allPaths = rootPaths;
    } else {
        rootPaths = Installable::toBuiltPaths(getEvalStore(), store, realiseMode, operateOn, installables);
        allPaths = rootPaths;

        if (recursive) {
            // XXX: This only computes the store path closure, ignoring
            // intermediate realisations
            StorePathSet pathsRoots, pathsClosure;
            for (auto & root : rootPaths) {
                auto rootFromThis = root.outPaths();
                pathsRoots.insert(rootFromThis.begin(), rootFromThis.end());
            }
            store->computeFSClosure(pathsRoots, pathsClosure);
            for (auto & path : pathsClosure)
                allPaths.emplace_back(BuiltPath::Opaque{path});
        }
    }

    run(store, std::move(allPaths), std::move(rootPaths));
}

StorePathsCommand::StorePathsCommand(bool recursive)
    : BuiltPathsCommand(recursive)
{
}

void StorePathsCommand::run(ref<Store> store, BuiltPaths && allPaths, BuiltPaths && rootPaths)
{
    StorePathSet storePaths;
    for (auto & builtPath : allPaths)
        for (auto & p : builtPath.outPaths())
            storePaths.insert(p);

    auto sorted = store->topoSortPaths(storePaths);
    std::reverse(sorted.begin(), sorted.end());

    run(store, std::move(sorted));
}

void StorePathCommand::run(ref<Store> store, StorePaths && storePaths)
{
    if (storePaths.size() != 1)
        throw UsageError("this command requires exactly one store path");

    run(store, *storePaths.begin());
}

MixProfile::MixProfile()
{
    addFlag({
        .longName = "profile",
        .description = "The profile to operate on.",
        .labels = {"path"},
        .handler = {&profile},
        .completer = completePath,
    });
}

void MixProfile::updateProfile(Store & store_, const StorePath & storePath)
{
    if (!profile)
        return;
    auto * store = dynamic_cast<LocalFSStore *>(&store_);
    if (!store)
        throw Error("'--profile' is not supported for this Nix store");
    auto profile2 = absPath(*profile);
    switchLink(profile2, createGeneration(*store, profile2, storePath));
}

void MixProfile::updateProfile(Store & store, const BuiltPaths & buildables)
{
    if (!profile)
        return;

    StorePaths result;

    for (auto & buildable : buildables) {
        std::visit(
            overloaded{
                [&](const BuiltPath::Opaque & bo) { result.push_back(bo.path); },
                [&](const BuiltPath::Built & bfd) {
                    for (auto & output : bfd.outputs) {
                        result.push_back(output.second);
                    }
                },
            },
            buildable.raw());
    }

    if (result.size() != 1)
        throw UsageError(
            "'--profile' requires that the arguments produce a single store path, but there are %d", result.size());

    updateProfile(store, result[0]);
}

MixDefaultProfile::MixDefaultProfile()
{
    profile = getDefaultProfile(settings.getProfileDirsOptions()).string();
}

static constexpr auto environmentVariablesCategory = "Options that change environment variables";

MixEnvironment::MixEnvironment()
    : ignoreEnvironment(false)
{
    addFlag({
        .longName = "ignore-env",
        .aliases = {"ignore-environment"},
        .shortName = 'i',
        .description = "Clear the entire environment, except for those specified with `--keep-env-var`.",
        .category = environmentVariablesCategory,
        .handler = {&ignoreEnvironment, true},
    });

    addFlag({
        .longName = "keep-env-var",
        .aliases = {"keep"},
        .shortName = 'k',
        .description = "Keep the environment variable *name*, when using `--ignore-env`.",
        .category = environmentVariablesCategory,
        .labels = {"name"},
        .handler = {[&](std::string s) { keepVars.insert(s); }},
    });

    addFlag({
        .longName = "unset-env-var",
        .aliases = {"unset"},
        .shortName = 'u',
        .description = "Unset the environment variable *name*.",
        .category = environmentVariablesCategory,
        .labels = {"name"},
        .handler = {[&](std::string name) {
            if (setVars.contains(name))
                throw UsageError("Cannot unset environment variable '%s' that is set with '%s'", name, "--set-env-var");

            unsetVars.insert(name);
        }},
    });

    addFlag({
        .longName = "set-env-var",
        .shortName = 's',
        .description = "Sets an environment variable *name* with *value*.",
        .category = environmentVariablesCategory,
        .labels = {"name", "value"},
        .handler = {[&](std::string name, std::string value) {
            if (unsetVars.contains(name))
                throw UsageError(
                    "Cannot set environment variable '%s' that is unset with '%s'", name, "--unset-env-var");

            if (setVars.contains(name))
                throw UsageError(
                    "Duplicate definition of environment variable '%s' with '%s' is ambiguous", name, "--set-env-var");

            setVars.insert_or_assign(name, value);
        }},
    });
}

void MixEnvironment::setEnviron()
{
    if (ignoreEnvironment && !unsetVars.empty())
        throw UsageError("--unset-env-var does not make sense with --ignore-env");

    if (!ignoreEnvironment && !keepVars.empty())
        throw UsageError("--keep-env-var does not make sense without --ignore-env");

    auto env = getEnv();

    if (ignoreEnvironment)
        std::erase_if(env, [&](const auto & var) { return !keepVars.contains(var.first); });

    for (const auto & [name, value] : setVars)
        env[name] = value;

    env["IN_NIX_SHELL"] = ignoreEnvironment ? "pure" : "impure";

    if (!unsetVars.empty())
        std::erase_if(env, [&](const auto & var) { return unsetVars.contains(var.first); });

    replaceEnv(env);

    return;
}

void createOutLinks(const std::filesystem::path & outLink, const BuiltPaths & buildables, LocalFSStore & store)
{
    for (const auto & [_i, buildable] : enumerate(buildables)) {
        auto i = _i;
        std::visit(
            overloaded{
                [&](const BuiltPath::Opaque & bo) {
                    auto symlink = outLink;
                    if (i)
                        symlink += fmt("-%d", i);
                    store.addPermRoot(bo.path, absPath(symlink).string());
                },
                [&](const BuiltPath::Built & bfd) {
                    for (auto & output : bfd.outputs) {
                        auto symlink = outLink;
                        if (i)
                            symlink += fmt("-%d", i);
                        if (output.first != "out")
                            symlink += fmt("-%s", output.first);
                        store.addPermRoot(output.second, absPath(symlink).string());
                    }
                },
            },
            buildable.raw());
    }
}

void MixOutLinkBase::createOutLinksMaybe(const std::vector<BuiltPathWithResult> & buildables, ref<Store> & store)
{
    if (outLink != "")
        if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
            createOutLinks(outLink, toBuiltPaths(buildables), *store2);
}

} // namespace nix
