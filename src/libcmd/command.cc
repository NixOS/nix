#include "command.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "derivations.hh"
#include "nixexpr.hh"
#include "profiles.hh"

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

EvalCommand::EvalCommand()
{
}

EvalCommand::~EvalCommand()
{
    if (evalState)
        evalState->printStats();
}

ref<Store> EvalCommand::getEvalStore()
{
    if (!evalStore)
        evalStore = evalStoreUrl ? openStore(*evalStoreUrl) : getStore();
    return ref<Store>(evalStore);
}

ref<EvalState> EvalCommand::getEvalState()
{
    if (!evalState)
        evalState = std::make_shared<EvalState>(searchPath, getEvalStore(), getStore());
    return ref<EvalState>(evalState);
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

void BuiltPathsCommand::run(ref<Store> store)
{
    BuiltPaths paths;
    if (all) {
        if (installables.size())
            throw UsageError("'--all' does not expect arguments");
        // XXX: Only uses opaque paths, ignores all the realisations
        for (auto & p : store->queryAllValidPaths())
            paths.push_back(BuiltPath::Opaque{p});
    } else {
        paths = toBuiltPaths(getEvalStore(), store, realiseMode, operateOn, installables);
        if (recursive) {
            // XXX: This only computes the store path closure, ignoring
            // intermediate realisations
            StorePathSet pathsRoots, pathsClosure;
            for (auto & root: paths) {
                auto rootFromThis = root.outPaths();
                pathsRoots.insert(rootFromThis.begin(), rootFromThis.end());
            }
            store->computeFSClosure(pathsRoots, pathsClosure);
            for (auto & path : pathsClosure)
                paths.push_back(BuiltPath::Opaque{path});
        }
    }

    run(store, std::move(paths));
}

StorePathsCommand::StorePathsCommand(bool recursive)
    : BuiltPathsCommand(recursive)
{
}

void StorePathsCommand::run(ref<Store> store, BuiltPaths paths)
{
    StorePaths storePaths;
    for (auto& builtPath : paths)
        for (auto& p : builtPath.outPaths())
            storePaths.push_back(p);

    run(store, std::move(storePaths));
}

void StorePathCommand::run(ref<Store> store, std::vector<StorePath> storePaths)
{
    if (storePaths.size() != 1)
        throw UsageError("this command requires exactly one store path");

    run(store, *storePaths.begin());
}

Strings editorFor(const Pos & pos)
{
    auto editor = getEnv("EDITOR").value_or("cat");
    auto args = tokenizeString<Strings>(editor);
    if (pos.line > 0 && (
        editor.find("emacs") != std::string::npos ||
        editor.find("nano") != std::string::npos ||
        editor.find("vim") != std::string::npos))
        args.push_back(fmt("+%d", pos.line));
    args.push_back(pos.file);
    return args;
}

MixProfile::MixProfile()
{
    addFlag({
        .longName = "profile",
        .description = "The profile to update.",
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
        createGeneration(
            ref<LocalFSStore>(store),
            profile2, storePath));
}

void MixProfile::updateProfile(const BuiltPaths & buildables)
{
    if (!profile) return;

    std::vector<StorePath> result;

    for (auto & buildable : buildables) {
        std::visit(overloaded {
            [&](BuiltPath::Opaque bo) {
                result.push_back(bo.path);
            },
            [&](BuiltPath::Built bfd) {
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
