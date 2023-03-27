#pragma once

#include "installables.hh"
#include "args.hh"
#include "common-eval-args.hh"
#include "path.hh"
#include "flake/lockfile.hh"

#include <optional>

namespace nix {

extern std::string programPath;

extern char * * savedArgv;

class EvalState;
struct Pos;
class Store;

static constexpr Command::Category catHelp = -1;
static constexpr Command::Category catSecondary = 100;
static constexpr Command::Category catUtility = 101;
static constexpr Command::Category catNixInstallation = 102;

static constexpr auto installablesCategory = "Options that change the interpretation of [installables](@docroot@/command-ref/new-cli/nix.md#installables)";

struct NixMultiCommand : virtual MultiCommand, virtual Command
{
    nlohmann::json toJSON() override;
};

// For the overloaded run methods
#pragma GCC diagnostic ignored "-Woverloaded-virtual"

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

/* A command that copies something between `--from` and `--to`
   stores. */
struct CopyCommand : virtual StoreCommand
{
    std::string srcUri, dstUri;

    CopyCommand();

    ref<Store> createStore() override;

    ref<Store> getDstStore();
};

struct EvalCommand : virtual StoreCommand, MixEvalArgs
{
    bool startReplOnEvalErrors = false;
    bool ignoreExceptionsDuringTry = false;

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

    std::optional<std::string> needsFlakeInputCompletion = {};

    MixFlakeOptions();

    virtual std::vector<std::string> getFlakesForCompletion()
    { return {}; }

    void completeFlakeInput(std::string_view prefix);

    void completionHook() override;
};

struct SourceExprCommand : virtual Args, MixFlakeOptions
{
    std::optional<Path> file;
    std::optional<std::string> expr;

    SourceExprCommand();

    Installables parseInstallables(
        ref<Store> store, std::vector<std::string> ss);

    ref<Installable> parseInstallable(
        ref<Store> store, const std::string & installable);

    virtual Strings getDefaultFlakeAttrPaths();

    virtual Strings getDefaultFlakeAttrPathPrefixes();

    void completeInstallable(std::string_view prefix);
};

struct MixReadOnlyOption : virtual Args
{
    MixReadOnlyOption();
};

/* Like InstallablesCommand but the installables are not loaded */
struct RawInstallablesCommand : virtual Args, SourceExprCommand
{
    RawInstallablesCommand();

    virtual void run(ref<Store> store, std::vector<std::string> && rawInstallables) = 0;

    void run(ref<Store> store) override;

    // FIXME make const after CmdRepl's override is fixed up
    virtual void applyDefaultInstallables(std::vector<std::string> & rawInstallables);

    bool readFromStdIn = false;

    std::vector<std::string> getFlakesForCompletion() override;

private:

    std::vector<std::string> rawInstallables;
};
/* A command that operates on a list of "installables", which can be
   store paths, attribute paths, Nix expressions, etc. */
struct InstallablesCommand : RawInstallablesCommand
{
    virtual void run(ref<Store> store, Installables && installables) = 0;

    void run(ref<Store> store, std::vector<std::string> && rawInstallables) override;
};

/* A command that operates on exactly one "installable" */
struct InstallableCommand : virtual Args, SourceExprCommand
{
    InstallableCommand();

    virtual void run(ref<Store> store, ref<Installable> installable) = 0;

    void run(ref<Store> store) override;

    std::vector<std::string> getFlakesForCompletion() override
    {
        return {_installable};
    }

private:

    std::string _installable{"."};
};

struct MixOperateOnOptions : virtual Args
{
    OperateOn operateOn = OperateOn::Output;

    MixOperateOnOptions();
};

/* A command that operates on zero or more store paths. */
struct BuiltPathsCommand : InstallablesCommand, virtual MixOperateOnOptions
{
private:

    bool recursive = false;
    bool all = false;

protected:

    Realise realiseMode = Realise::Derivation;

public:

    BuiltPathsCommand(bool recursive = false);

    virtual void run(ref<Store> store, BuiltPaths && paths) = 0;

    void run(ref<Store> store, Installables && installables) override;

    void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override;
};

struct StorePathsCommand : public BuiltPathsCommand
{
    StorePathsCommand(bool recursive = false);

    virtual void run(ref<Store> store, StorePaths && storePaths) = 0;

    void run(ref<Store> store, BuiltPaths && paths) override;
};

/* A command that operates on exactly one store path. */
struct StorePathCommand : public StorePathsCommand
{
    virtual void run(ref<Store> store, const StorePath & storePath) = 0;

    void run(ref<Store> store, StorePaths && storePaths) override;
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
