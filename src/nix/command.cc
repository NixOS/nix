#include "command.hh"
#include "store-api.hh"
#include "derivations.hh"

namespace nix {

Commands * RegisterCommand::commands = 0;

void Command::printHelp(const string & programName, std::ostream & out)
{
    Args::printHelp(programName, out);

    auto exs = examples();
    if (!exs.empty()) {
        out << "\n";
        out << "Examples:\n";
        for (auto & ex : exs)
            out << "\n"
                << "  " << ex.description << "\n" // FIXME: wrap
                << "  $ " << ex.command << "\n";
    }
}

MultiCommand::MultiCommand(const Commands & _commands)
    : commands(_commands)
{
    expectedArgs.push_back(ExpectedArg{"command", 1, [=](Strings ss) {
        assert(!command);
        auto i = commands.find(ss.front());
        if (i == commands.end())
            throw UsageError(format("'%1%' is not a recognised command") % ss.front());
        command = i->second;
    }});
}

void MultiCommand::printHelp(const string & programName, std::ostream & out)
{
    if (command) {
        command->printHelp(programName + " " + command->name(), out);
        return;
    }

    out << "Usage: " << programName << " <COMMAND> <FLAGS>... <ARGS>...\n";

    out << "\n";
    out << "Common flags:\n";
    printFlags(out);

    out << "\n";
    out << "Available commands:\n";

    Table2 table;
    for (auto & command : commands)
        table.push_back(std::make_pair(command.second->name(), command.second->description()));
    printTable(out, table);

    out << "\n";
    out << "For full documentation, run 'man " << programName << "' or 'man " << programName << "-<COMMAND>'.\n";
}

bool MultiCommand::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    if (Args::processFlag(pos, end)) return true;
    if (command && command->processFlag(pos, end)) return true;
    return false;
}

bool MultiCommand::processArgs(const Strings & args, bool finish)
{
    if (command)
        return command->processArgs(args, finish);
    else
        return Args::processArgs(args, finish);
}

StoreCommand::StoreCommand()
{
    storeUri = getEnv("NIX_REMOTE");

    mkFlag(0, "store", "store-uri", "URI of the Nix store to use", &storeUri);
}

ref<Store> StoreCommand::getStore()
{
    if (!_store)
        _store = createStore();
    return ref<Store>(_store);
}

ref<Store> StoreCommand::createStore()
{
    return openStore(storeUri);
}

void StoreCommand::run()
{
    run(createStore());
}

StorePathsCommand::StorePathsCommand()
{
    mkFlag('r', "recursive", "apply operation to closure of the specified paths", &recursive);
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
        for (auto & p : buildInstallables(store, false))
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
    auto storePaths = buildInstallables(store, false);

    if (storePaths.size() != 1)
        throw UsageError("this command requires exactly one store path");

    run(store, *storePaths.begin());
}

}
