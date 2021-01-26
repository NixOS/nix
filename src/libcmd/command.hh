#pragma once

#include "args.hh"
#include "common-eval-args.hh"

namespace nix {

extern std::string programPath;

struct Value;
class Bindings;
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
    StoreCommand();
    void run() override;
    ref<Store> getStore();
    virtual ref<Store> createStore();
    virtual void run(ref<Store>) = 0;

private:
    std::shared_ptr<Store> _store;
};

struct Buildable
{
    Path drvPath; // may be empty
    std::map<std::string, Path> outputs;
};

typedef std::vector<Buildable> Buildables;

struct Installable
{
    virtual std::string what() = 0;

    virtual Buildables toBuildables()
    {
        throw Error("argument '%s' cannot be built", what());
    }

    Buildable toBuildable();

    virtual Value * toValue(EvalState & state)
    {
        throw Error("argument '%s' cannot be evaluated", what());
    }
};

struct SourceExprCommand : virtual Args, StoreCommand, MixEvalArgs
{
    Path file;

    SourceExprCommand();

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

enum RealiseMode { Build, NoBuild, DryRun };

/* A command that operates on a list of "installables", which can be
   store paths, attribute paths, Nix expressions, etc. */
struct InstallablesCommand : virtual Args, SourceExprCommand
{
    std::vector<std::shared_ptr<Installable>> installables;

    InstallablesCommand()
    {
        expectArgs("installables", &_installables);
    }

    void prepare() override;

    virtual bool useDefaultInstallables() { return true; }

private:

    std::vector<std::string> _installables;
};

struct InstallableCommand : virtual Args, SourceExprCommand
{
    std::shared_ptr<Installable> installable;

    InstallableCommand()
    {
        expectArg("installable", &_installable);
    }

    void prepare() override;

private:

    std::string _installable;
};

/* A command that operates on zero or more store paths. */
struct StorePathsCommand : public InstallablesCommand
{
private:

    bool recursive = false;
    bool all = false;

public:

    StorePathsCommand(bool recursive = false);

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

std::shared_ptr<Installable> parseInstallable(
    SourceExprCommand & cmd, ref<Store> store, const std::string & installable,
    bool useDefaultInstallables);

Buildables build(ref<Store> store, RealiseMode mode,
    std::vector<std::shared_ptr<Installable>> installables);

PathSet toStorePaths(ref<Store> store, RealiseMode mode,
    std::vector<std::shared_ptr<Installable>> installables);

Path toStorePath(ref<Store> store, RealiseMode mode,
    std::shared_ptr<Installable> installable);

PathSet toDerivations(ref<Store> store,
    std::vector<std::shared_ptr<Installable>> installables,
    bool useDeriver = false);

}
