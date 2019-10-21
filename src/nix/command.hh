#pragma once

#include "installables.hh"
#include "args.hh"
#include "common-eval-args.hh"

#include <optional>

namespace nix {

extern std::string programPath;

class EvalState;
class Store;

namespace flake {
enum HandleLockFile : unsigned int;
}

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

struct EvalCommand : virtual StoreCommand, MixEvalArgs
{
    ref<EvalState> getEvalState();

private:

    std::shared_ptr<EvalState> evalState;
};

struct MixFlakeOptions : virtual Args
{
    bool recreateLockFile = false;

    bool saveLockFile = true;

    bool useRegistries = true;

    MixFlakeOptions();

    flake::HandleLockFile getLockFileMode();
};

struct SourceExprCommand : virtual Args, EvalCommand, MixFlakeOptions
{
    std::optional<Path> file;

    SourceExprCommand();

    std::vector<std::shared_ptr<Installable>> parseInstallables(
        ref<Store> store, std::vector<std::string> ss);

    std::shared_ptr<Installable> parseInstallable(
        ref<Store> store, const std::string & installable);

    virtual Strings getDefaultFlakeAttrPaths();

    virtual Strings getDefaultFlakeAttrPathPrefixes();
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
        expectArg("installable", &_installable, true);
    }

    void prepare() override;

private:

    std::string _installable{"."};
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

Buildables build(ref<Store> store, RealiseMode mode,
    std::vector<std::shared_ptr<Installable>> installables);

PathSet toStorePaths(ref<Store> store, RealiseMode mode,
    std::vector<std::shared_ptr<Installable>> installables);

Path toStorePath(ref<Store> store, RealiseMode mode,
    std::shared_ptr<Installable> installable);

PathSet toDerivations(ref<Store> store,
    std::vector<std::shared_ptr<Installable>> installables,
    bool useDeriver = false);

struct MixProfile : virtual Args, virtual StoreCommand
{
    std::optional<Path> profile;

    MixProfile();

    /* If 'profile' is set, make it point at 'storePath'. */
    void updateProfile(const Path & storePath);

    /* If 'profile' is set, make it point at the store path produced
       by 'buildables'. */
    void updateProfile(const Buildables & buildables);
};

}
