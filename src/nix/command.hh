#pragma once

#include "args.hh"
#include "common-eval-args.hh"

namespace nix {

extern std::string programPath;

struct Value;
class Bindings;
class EvalState;

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

struct GitRepoCommand : virtual Args
{
    std::string gitPath = absPath(".");

    GitRepoCommand ()
    {
        expectArg("git-path", &gitPath, true);
    }
};

struct FlakeCommand : virtual Args, StoreCommand, MixEvalArgs
{
    std::string flakeUri;

    FlakeCommand()
    {
        expectArg("flake-uri", &flakeUri);
    }
};

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
    std::optional<Path> file;
    std::optional<std::string> flakeUri;

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

/* A helper class for registering commands globally. */
struct RegisterCommand
{
    static std::vector<ref<Command>> * commands;

    RegisterCommand(ref<Command> command)
    {
        if (!commands) commands = new std::vector<ref<Command>>;
        commands->push_back(command);
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
