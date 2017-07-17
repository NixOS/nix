#pragma once

#include "args.hh"

namespace nix {

struct Value;
class EvalState;

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
    ref<Store> getStore();
    virtual ref<Store> createStore();
    virtual void run(ref<Store>) = 0;

private:
    std::shared_ptr<Store> _store;
};

struct Whence { std::string outputName; Path drvPath; };
typedef std::map<Path, Whence> Buildables;

struct Installable
{
    virtual std::string what() = 0;

    virtual Buildables toBuildable()
    {
        throw Error("argument ‘%s’ cannot be built", what());
    }

    virtual Value * toValue(EvalState & state)
    {
        throw Error("argument ‘%s’ cannot be evaluated", what());
    }
};

struct SourceExprCommand : virtual Args, StoreCommand
{
    Path file;

    SourceExprCommand()
    {
        mkFlag('f', "file", "file", "evaluate FILE rather than the default", &file);
    }

    /* Return a value representing the Nix expression from which we
       are installing. This is either the file specified by ‘--file’,
       or an attribute set constructed from $NIX_PATH, e.g. ‘{ nixpkgs
       = import ...; bla = import ...; }’. */
    Value * getSourceExpr(EvalState & state);

    ref<EvalState> getEvalState();

private:

    std::shared_ptr<EvalState> evalState;

    Value * vSourceExpr = 0;
};

/* A command that operates on a list of "installables", which can be
   store paths, attribute paths, Nix expressions, etc. */
struct InstallablesCommand : virtual Args, SourceExprCommand
{
    std::vector<std::shared_ptr<Installable>> installables;

    InstallablesCommand()
    {
        expectArgs("installables", &_installables);
    }

    std::vector<std::shared_ptr<Installable>> parseInstallables(ref<Store> store, Strings ss);

    enum ToStorePathsMode { Build, NoBuild, DryRun };

    PathSet toStorePaths(ref<Store> store, ToStorePathsMode mode);

    void prepare() override;

    virtual bool useDefaultInstallables() { return true; }

private:

    Strings _installables;
};

/* A command that operates on zero or more store paths. */
struct StorePathsCommand : public InstallablesCommand
{
private:

    bool recursive = false;
    bool all = false;

public:

    StorePathsCommand();

    using StoreCommand::run;

    virtual void run(ref<Store> store, Paths storePaths) = 0;

    void run(ref<Store> store) override;

    bool useDefaultInstallables() override { return !all; }
};

/* A command that operates on exactly one store path. */
struct StorePathCommand : public InstallablesCommand
{
    using StoreCommand::run;

    virtual void run(ref<Store> store, const Path & storePath) = 0;

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
