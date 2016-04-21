#pragma once

#include "args.hh"

namespace nix {

/* A command is an argument parser that can be executed by calling its
   run() method. */
struct Command : virtual Args
{
    virtual std::string name() = 0;
    virtual void prepare() { };
    virtual void run() = 0;

    struct Example
    {
        std::string description;
        std::string command;
    };

    typedef std::list<Example> Examples;

    virtual Examples examples() { return Examples(); }

    void printHelp(const string & programName, std::ostream & out) override;
};

class Store;

/* A command that require a Nix store. */
struct StoreCommand : virtual Command
{
    std::string storeUri;
    StoreCommand();
    void run() override;
    virtual void run(ref<Store>) = 0;
};

/* A command that operates on zero or more store paths. */
struct StorePathsCommand : public StoreCommand
{
private:

    Paths storePaths;
    bool recursive = false;
    bool all = false;

public:

    StorePathsCommand();

    virtual void run(ref<Store> store, Paths storePaths) = 0;

    void run(ref<Store> store) override;
};

typedef std::map<std::string, ref<Command>> Commands;

/* An argument parser that supports multiple subcommands,
   i.e. ‘<command> <subcommand>’. */
class MultiCommand : virtual Args
{
public:
    Commands commands;

    std::shared_ptr<Command> command;

    MultiCommand(const Commands & commands);

    void printHelp(const string & programName, std::ostream & out) override;

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;
};

/* A helper class for registering commands globally. */
struct RegisterCommand
{
    static Commands * commands;

    RegisterCommand(ref<Command> command)
    {
        if (!commands) commands = new Commands;
        commands->emplace(command->name(), command);
    }
};

}
