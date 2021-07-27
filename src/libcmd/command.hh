#pragma once

#include "installables.hh"
#include "args.hh"
#include "common-eval-args.hh"
#include "path.hh"
#include "flake/lockfile.hh"
#include "store-api.hh"

#include <optional>

namespace nix {

extern std::string programPath;

extern char * * savedArgv;

class EvalState;
struct Pos;
class Store;

static constexpr Command::Category catSecondary = 100;
static constexpr Command::Category catUtility = 101;
static constexpr Command::Category catNixInstallation = 102;

static constexpr auto installablesCategory = "Options that change the interpretation of installables";

struct NixMultiCommand : virtual MultiCommand, virtual Command
{
    nlohmann::json toJSON() override;
};

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

struct EvalCommand : virtual StoreCommand, MixEvalArgs
{
    EvalCommand();

    ~EvalCommand();

    ref<Store> getEvalStore();

    ref<EvalState> getEvalState();

private:
    std::shared_ptr<Store> evalStore;

    std::shared_ptr<EvalState> evalState;
};

struct MixFlakeOptions : virtual Args, EvalCommand
{
    flake::LockFlags lockFlags;

    MixFlakeOptions();

    virtual std::optional<FlakeRef> getFlakeRefForCompletion()
    { return {}; }
};

/* How to handle derivations in commands that operate on store paths. */
enum class OperateOn {
    /* Operate on the output path. */
    Output,
    /* Operate on the .drv path. */
    Derivation
};

struct SourceExprCommand : virtual Args, MixFlakeOptions
{
    std::optional<Path> file;
    std::optional<std::string> expr;

    // FIXME: move this; not all commands (e.g. 'nix run') use it.
    OperateOn operateOn = OperateOn::Output;

    SourceExprCommand();

    std::vector<std::shared_ptr<Installable>> parseInstallables(
        ref<Store> store, std::vector<std::string> ss);

    std::shared_ptr<Installable> parseInstallable(
        ref<Store> store, const std::string & installable);

    virtual Strings getDefaultFlakeAttrPaths();

    virtual Strings getDefaultFlakeAttrPathPrefixes();

    void completeInstallable(std::string_view prefix);
};

enum class Realise {
    /* Build the derivation. Postcondition: the
       derivation outputs exist. */
    Outputs,
    /* Don't build the derivation. Postcondition: the store derivation
       exists. */
    Derivation,
    /* Evaluate in dry-run mode. Postcondition: nothing. */
    Nothing
};

/* A command that operates on a list of "installables", which can be
   store paths, attribute paths, Nix expressions, etc. */
struct InstallablesCommand : virtual Args, SourceExprCommand
{
    std::vector<std::shared_ptr<Installable>> installables;

    InstallablesCommand();

    void prepare() override;

    virtual bool useDefaultInstallables() { return true; }

    std::optional<FlakeRef> getFlakeRefForCompletion() override;

private:

    std::vector<std::string> _installables;
};

/* A command that operates on exactly one "installable" */
struct InstallableCommand : virtual Args, SourceExprCommand
{
    std::shared_ptr<Installable> installable;

    InstallableCommand();

    void prepare() override;

    std::optional<FlakeRef> getFlakeRefForCompletion() override
    {
        return parseFlakeRef(_installable, absPath("."));
    }

private:

    std::string _installable{"."};
};

/* A command that operates on zero or more store paths. */
struct BuiltPathsCommand : public InstallablesCommand
{
private:

    bool recursive = false;
    bool all = false;

protected:

    Realise realiseMode = Realise::Derivation;

public:

    BuiltPathsCommand(bool recursive = false);

    using StoreCommand::run;

    virtual void run(ref<Store> store, BuiltPaths paths) = 0;

    void run(ref<Store> store) override;

    bool useDefaultInstallables() override { return !all; }
};

struct StorePathsCommand : public BuiltPathsCommand
{
    StorePathsCommand(bool recursive = false);

    using BuiltPathsCommand::run;

    virtual void run(ref<Store> store, std::vector<StorePath> storePaths) = 0;

    void run(ref<Store> store, BuiltPaths paths) override;
};

/* A command that operates on exactly one store path. */
struct StorePathCommand : public StorePathsCommand
{
    using StorePathsCommand::run;

    virtual void run(ref<Store> store, const StorePath & storePath) = 0;

    void run(ref<Store> store, std::vector<StorePath> storePaths) override;
};

/* A helper class for registering commands globally. */
struct RegisterCommand
{
    typedef std::map<std::vector<std::string>, std::function<ref<Command>()>> Commands;
    static Commands * commands;

    RegisterCommand(std::vector<std::string> && name,
        std::function<ref<Command>()> command)
    {
        if (!commands) commands = new Commands;
        commands->emplace(name, command);
    }

    static nix::Commands getCommandsFor(const std::vector<std::string> & prefix);
};

template<class T>
static RegisterCommand registerCommand(const std::string & name)
{
    return RegisterCommand({name}, [](){ return make_ref<T>(); });
}

template<class T>
static RegisterCommand registerCommand2(std::vector<std::string> && name)
{
    return RegisterCommand(std::move(name), [](){ return make_ref<T>(); });
}

BuiltPaths build(ref<Store> evalStore, ref<Store> store, Realise mode,
    std::vector<std::shared_ptr<Installable>> installables, BuildMode bMode = bmNormal);

std::set<StorePath> toStorePaths(
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode,
    OperateOn operateOn,
    std::vector<std::shared_ptr<Installable>> installables);

StorePath toStorePath(
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode,
    OperateOn operateOn,
    std::shared_ptr<Installable> installable);

std::set<StorePath> toDerivations(ref<Store> store,
    std::vector<std::shared_ptr<Installable>> installables,
    bool useDeriver = false);

BuiltPaths toBuiltPaths(
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode,
    OperateOn operateOn,
    std::vector<std::shared_ptr<Installable>> installables);

/* Helper function to generate args that invoke $EDITOR on
   filename:lineno. */
Strings editorFor(const Pos & pos);

struct MixProfile : virtual StoreCommand
{
    std::optional<Path> profile;

    MixProfile();

    /* If 'profile' is set, make it point at 'storePath'. */
    void updateProfile(const StorePath & storePath);

    /* If 'profile' is set, make it point at the store path produced
       by 'buildables'. */
    void updateProfile(const BuiltPaths & buildables);
};

struct MixDefaultProfile : MixProfile
{
    MixDefaultProfile();
};

struct MixEnvironment : virtual Args {

    StringSet keep, unset;
    Strings stringsEnv;
    std::vector<char*> vectorEnv;
    bool ignoreEnvironment;

    MixEnvironment();

    /* Modify global environ based on ignoreEnvironment, keep, and unset. It's expected that exec will be called before this class goes out of scope, otherwise environ will become invalid. */
    void setEnviron();
};

void completeFlakeRef(ref<Store> store, std::string_view prefix);

void completeFlakeRefWithFragment(
    ref<EvalState> evalState,
    flake::LockFlags lockFlags,
    Strings attrPathPrefixes,
    const Strings & defaultFlakeAttrPaths,
    std::string_view prefix);

std::string showVersions(const std::set<std::string> & versions);

void printClosureDiff(
    ref<Store> store,
    const StorePath & beforePath,
    const StorePath & afterPath,
    std::string_view indent);

}
