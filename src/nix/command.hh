#pragma once

#include "args.hh"
#include "common-eval-args.hh"
#include "path.hh"

namespace nix {

extern std::string programPath;

struct Value;
class Bindings;
class EvalState;
struct Pos;
class Store;

/* A command that requires a Nix store. */
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
    std::optional<StorePath> drvPath;
    std::map<std::string, StorePath> outputs;
};

typedef std::vector<Buildable> Buildables;

struct Installable
{
    virtual ~Installable() { }

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

/* A command that operates on exactly one "installable" */
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

protected:

    RealiseMode realiseMode = NoBuild;

public:

    StorePathsCommand(bool recursive = false);

    using StoreCommand::run;

    virtual void run(ref<Store> store, std::vector<StorePath> storePaths) = 0;

    void run(ref<Store> store) override;

    bool useDefaultInstallables() override { return !all; }
};

/* A command that operates on exactly one store path. */
struct StorePathCommand : public InstallablesCommand
{
    using StoreCommand::run;

    virtual void run(ref<Store> store, const StorePath & storePath) = 0;

    void run(ref<Store> store) override;
};

/* A helper class for registering commands globally. */
struct RegisterCommand
{
    static Commands * commands;

    RegisterCommand(const std::string & name,
        std::function<ref<Command>()> command)
    {
        if (!commands) commands = new Commands;
        commands->emplace(name, command);
    }
};

template<class T>
static RegisterCommand registerCommand(const std::string & name)
{
    return RegisterCommand(name, [](){ return make_ref<T>(); });
}

std::shared_ptr<Installable> parseInstallable(
    SourceExprCommand & cmd, ref<Store> store, const std::string & installable,
    bool useDefaultInstallables);

Buildables build(ref<Store> store, RealiseMode mode,
    std::vector<std::shared_ptr<Installable>> installables);

std::set<StorePath> toStorePaths(ref<Store> store, RealiseMode mode,
    std::vector<std::shared_ptr<Installable>> installables);

StorePath toStorePath(ref<Store> store, RealiseMode mode,
    std::shared_ptr<Installable> installable);

std::set<StorePath> toDerivations(ref<Store> store,
    std::vector<std::shared_ptr<Installable>> installables,
    bool useDeriver = false);

/* Helper function to generate args that invoke $EDITOR on
   filename:lineno. */
Strings editorFor(const Pos & pos);

}
