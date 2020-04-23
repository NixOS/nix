#include "command.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "nixexpr.hh"
#include "eval.hh"

namespace nix {

Commands * RegisterCommand::commands = nullptr;

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

StorePathsCommand::StorePathsCommand(bool recursive)
    : recursive(recursive)
{
    if (recursive)
        mkFlag()
            .longName("no-recursive")
            .description("apply operation to specified paths only")
            .set(&this->recursive, false);
    else
        mkFlag()
            .longName("recursive")
            .shortName('r')
            .description("apply operation to closure of the specified paths")
            .set(&this->recursive, true);

    mkFlag(0, "all", "apply operation to the entire store", &all);

    // these options only make sense when recursing
    mkFlag('b', "build", "include build dependencies of specified path", &includeBuildDeps);
    mkFlag('e', "eval", "include eval dependencies of specified path", &includeEvalDeps);
}

void StorePathsCommand::run(ref<Store> store)
{
    StorePaths storePaths;

    if (all) {
        if (installables.size())
            throw UsageError("'--all' does not expect arguments");
        for (auto & p : store->queryAllValidPaths())
            storePaths.push_back(p.clone());
    }

    else {
        if (!recursive && (includeBuildDeps || includeEvalDeps))
            throw UsageError("--build and --eval require --recursive");

        for (auto & p : toStorePaths(store, realiseMode, installables))
            storePaths.push_back(p.clone());

        if (recursive) {
            if (includeEvalDeps) {
                for (auto & i : installables) {
                    auto state = getEvalState();

                    for (auto & b : i->toBuildables()) {
                        if (!b.drvPath)
                            throw UsageError("Cannot find eval references without a derivation path");
                    }

                    // force evaluation of package argument
                    i->toValue(*state);

                    for (auto & d : (*state).importedDrvs)
                        storePaths.push_back(store->parseStorePath(d.first));
                }
            }

            if (includeBuildDeps) {
                for (auto & i : installables) {
                    for (auto & b : i->toBuildables()) {
                        if (!b.drvPath) // Note we could lookup deriver from the DB to get drvPath
                            throw UsageError("Cannot find build references without a derivation path");
                        storePaths.push_back(b.drvPath->clone());
                    }
                }
            }

            auto includeDerivers = includeBuildDeps || includeEvalDeps;

            StorePathSet closure;
            store->computeFSClosure(storePathsToSet(storePaths), closure, false, includeDerivers);
            storePaths.clear();
            for (auto & p : closure)
                if (!(includeDerivers && p.isDerivation()))
                    storePaths.push_back(p.clone());
        }
    }

    run(store, std::move(storePaths));
}

void StorePathCommand::run(ref<Store> store)
{
    auto storePaths = toStorePaths(store, NoBuild, installables);

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

}
