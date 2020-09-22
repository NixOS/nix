#include "command.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "nixexpr.hh"
#include "profiles.hh"

#include <nlohmann/json.hpp>

extern char * * environ __attribute__((weak));

namespace nix {

Commands * RegisterCommand::commands = nullptr;

void NixMultiCommand::printHelp(const string & programName, std::ostream & out)
{
    MultiCommand::printHelp(programName, out);
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
    auto store = getStore();
    run(store);
}

StorePathsCommand::StorePathsCommand(bool recursive)
    : recursive(recursive)
{
    if (recursive)
        addFlag({
            .longName = "no-recursive",
            .description = "apply operation to specified paths only",
            .handler = {&this->recursive, false},
        });
    else
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "apply operation to closure of the specified paths",
            .handler = {&this->recursive, true},
        });

    mkFlag(0, "all", "apply operation to the entire store", &all);
}

void StorePathsCommand::run(ref<Store> store)
{
    StorePaths storePaths;

    if (all) {
        if (installables.size())
            throw UsageError("'--all' does not expect arguments");
        for (auto & p : store->queryAllValidPaths())
            storePaths.push_back(p);
    }

    else {
        for (auto & p : toStorePaths(store, realiseMode, operateOn, installables))
            storePaths.push_back(p);

        if (recursive) {
            StorePathSet closure;
            store->computeFSClosure(StorePathSet(storePaths.begin(), storePaths.end()), closure, false, false);
            storePaths.clear();
            for (auto & p : closure)
                storePaths.push_back(p);
        }
    }

    run(store, std::move(storePaths));
}

void StorePathCommand::run(ref<Store> store)
{
    auto storePaths = toStorePaths(store, Realise::Nothing, operateOn, installables);

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
        .description = "profile to update",
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

void MixProfile::updateProfile(const Buildables & buildables)
{
    if (!profile) return;

    std::vector<StorePath> result;

    for (auto & buildable : buildables) {
        std::visit(overloaded {
            [&](BuildableOpaque bo) {
                result.push_back(bo.path);
            },
            [&](BuildableFromDrv bfd) {
                for (auto & output : bfd.outputs) {
                    /* Output path should be known because we just tried to
                       build it. */
                    assert(!output.second);
                    result.push_back(*output.second);
                }
            },
        }, buildable);
    }

    if (result.size() != 1)
        throw Error("'--profile' requires that the arguments produce a single store path, but there are %d", result.size());

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
        .description = "clear the entire environment (except those specified with --keep)",
        .handler = {&ignoreEnvironment, true},
    });

    addFlag({
        .longName = "keep",
        .shortName = 'k',
        .description = "keep specified environment variable",
        .labels = {"name"},
        .handler = {[&](std::string s) { keep.insert(s); }},
    });

    addFlag({
        .longName = "unset",
        .shortName = 'u',
        .description = "unset specified environment variable",
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
