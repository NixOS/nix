#include "command.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "nixexpr.hh"

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
}

void StorePathsCommand::run(ref<Store> store)
{
    Paths storePaths;

    if (all) {
        if (installables.size())
            throw UsageError("'--all' does not expect arguments");
        for (auto & p : store->queryAllValidPaths())
            storePaths.push_back(p);
    }

    else {
        for (auto & p : toStorePaths(store, realiseMode, installables))
            storePaths.push_back(p);

        if (recursive) {
            PathSet closure;
            store->computeFSClosure(PathSet(storePaths.begin(), storePaths.end()),
                closure, false, false);
            storePaths = Paths(closure.begin(), closure.end());
        }
    }

    run(store, storePaths);
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
