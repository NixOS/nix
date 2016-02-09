#include "command.hh"
#include "store-api.hh"

namespace nix {

Commands * RegisterCommand::commands = 0;

MultiCommand::MultiCommand(const Commands & _commands)
    : commands(_commands)
{
    expectedArgs.push_back(ExpectedArg{"command", 1, [=](Strings ss) {
        assert(!command);
        auto i = commands.find(ss.front());
        if (i == commands.end())
            throw UsageError(format("‘%1%’ is not a recognised command") % ss.front());
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
    out << "For full documentation, run ‘man " << programName << "’ or ‘man " << programName << "-<COMMAND>’.\n";
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

void StoreCommand::run()
{
    run(openStore(reserveSpace));
}

}
