#include "command.hh"
#include "markdown.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "derivations.hh"
#include "nixexpr.hh"
#include "profiles.hh"
#include "repl.hh"

#include <nlohmann/json.hpp>

extern char * * environ __attribute__((weak));

namespace nix {

RegisterCommand::Commands * RegisterCommand::commands = nullptr;

nix::Commands RegisterCommand::getCommandsFor(const std::vector<std::string> & prefix)
{
    nix::Commands res;
    for (auto & [name, command] : *RegisterCommand::commands)
        if (name.size() == prefix.size() + 1) {
            bool equal = true;
            for (size_t i = 0; i < prefix.size(); ++i)
                if (name[i] != prefix[i]) equal = false;
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
        std::set<std::string> subCommandTextLines;
        for (auto & [name, _] : commands)
            subCommandTextLines.insert(fmt("- `%s`", name));
        std::string markdownError = fmt("`nix %s` requires a sub-command. Available sub-commands:\n\n%s\n",
                commandName, concatStringsSep("\n", subCommandTextLines));
        throw UsageError(renderMarkdownToTerminal(markdownError));
    }
    command->second->run();
}

StoreCommand::StoreCommand()
{
}

ref<Store> StoreCommand::getStore()
{
    if (!_store)
        _store = createStore();
    return ref<Store>(_store);
}

ref<Store> StoreCommand::createStore()
{
    return openStore();
}

void StoreCommand::run()
{
    run(getStore());
}

CopyCommand::CopyCommand()
{
    addFlag({
        .longName = "from",
        .description = "URL of the source Nix store.",
        .labels = {"store-uri"},
        .handler = {&srcUri},
    });

    addFlag({
        .longName = "to",
        .description = "URL of the destination Nix store.",
        .labels = {"store-uri"},
        .handler = {&dstUri},
    });
}

ref<Store> CopyCommand::createStore()
{
    return srcUri.empty() ? StoreCommand::createStore() : openStore(srcUri);
}

ref<Store> CopyCommand::getDstStore()
{
    if (srcUri.empty() && dstUri.empty())
        throw UsageError("you must pass '--from' and/or '--to'");

    return dstUri.empty() ? openStore() : openStore(dstUri);
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
        evalStore = evalStoreUrl ? openStore(*evalStoreUrl) : getStore();
    return ref<Store>(evalStore);
}

ref<EvalState> EvalCommand::getEvalState()
{
    if (!evalState) {
        evalState =
            #if HAVE_BOEHMGC
            std::allocate_shared<EvalState>(traceable_allocator<EvalState>(),
                searchPath, getEvalStore(), getStore())
            #else
            std::make_shared<EvalState>(
                searchPath, getEvalStore(), getStore())
            #endif
            ;

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
        .description = "Operate on the [store derivation](@docroot@/glossary.md#gloss-store-derivation) rather than its outputs.",
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
    BuiltPaths paths;
    if (all) {
        if (installables.size())
            throw UsageError("'--all' does not expect arguments");
        // XXX: Only uses opaque paths, ignores all the realisations
        for (auto & p : store->queryAllValidPaths())
            paths.emplace_back(BuiltPath::Opaque{p});
    } else {
        paths = Installable::toBuiltPaths(getEvalStore(), store, realiseMode, operateOn, installables);
        if (recursive) {
            // XXX: This only computes the store path closure, ignoring
            // intermediate realisations
            StorePathSet pathsRoots, pathsClosure;
            for (auto & root : paths) {
                auto rootFromThis = root.outPaths();
                pathsRoots.insert(rootFromThis.begin(), rootFromThis.end());
            }
            store->computeFSClosure(pathsRoots, pathsClosure);
            for (auto & path : pathsClosure)
                paths.emplace_back(BuiltPath::Opaque{path});
        }
    }

    run(store, std::move(paths));
}

StorePathsCommand::StorePathsCommand(bool recursive)
    : BuiltPathsCommand(recursive)
{
}

void StorePathsCommand::run(ref<Store> store, BuiltPaths && paths)
{
    StorePathSet storePaths;
    for (auto & builtPath : paths)
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
        .completer = completePath
    });
}

void MixProfile::updateProfile(const StorePath & storePath)
{
    if (!profile) return;
    auto store = getStore().dynamic_pointer_cast<LocalFSStore>();
    if (!store) throw Error("'--profile' is not supported for this Nix store");
    auto profile2 = absPath(*profile);
    switchLink(profile2,
        createGeneration(*store, profile2, storePath));
}

void MixProfile::updateProfile(const BuiltPaths & buildables)
{
    if (!profile) return;

    StorePaths result;

    for (auto & buildable : buildables) {
        std::visit(overloaded {
            [&](const BuiltPath::Opaque & bo) {
                result.push_back(bo.path);
            },
            [&](const BuiltPath::Built & bfd) {
                for (auto & output : bfd.outputs) {
                    result.push_back(output.second);
                }
            },
        }, buildable.raw());
    }

    if (result.size() != 1)
        throw UsageError("'--profile' requires that the arguments produce a single store path, but there are %d", result.size());

    updateProfile(result[0]);
}

MixDefaultProfile::MixDefaultProfile()
{
    profile = getDefaultProfile();
}

MixEnvironment::MixEnvironment() : ignoreEnvironment(false)
{
    addFlag({
        .longName = "ignore-environment",
        .shortName = 'i',
        .description = "Clear the entire environment (except those specified with `--keep`).",
        .handler = {&ignoreEnvironment, true},
    });

    addFlag({
        .longName = "keep",
        .shortName = 'k',
        .description = "Keep the environment variable *name*.",
        .labels = {"name"},
        .handler = {[&](std::string s) { keep.insert(s); }},
    });

    addFlag({
        .longName = "unset",
        .shortName = 'u',
        .description = "Unset the environment variable *name*.",
        .labels = {"name"},
        .handler = {[&](std::string s) { unset.insert(s); }},
    });
}

void MixEnvironment::setEnviron() {
    if (ignoreEnvironment) {
        if (!unset.empty())
            throw UsageError("--unset does not make sense with --ignore-environment");

        for (const auto & var : keep) {
            auto val = getenv(var.c_str());
            if (val) stringsEnv.emplace_back(fmt("%s=%s", var.c_str(), val));
        }

        vectorEnv = stringsToCharPtrs(stringsEnv);
        environ = vectorEnv.data();
    } else {
        if (!keep.empty())
            throw UsageError("--keep does not make sense without --ignore-environment");

        for (const auto & var : unset)
            unsetenv(var.c_str());
    }
}

}
