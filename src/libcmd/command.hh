#pragma once
///@file

#include "installable-value.hh"
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

struct NixMultiCommand : MultiCommand, virtual Command
{
    nlohmann::json toJSON() override;

    using MultiCommand::MultiCommand;

    virtual void run() override;
};

// For the overloaded run methods
#pragma GCC diagnostic ignored "-Woverloaded-virtual"

/**
 * A command that requires a \ref Store "Nix store".
 */
struct StoreCommand : virtual Command
{
    StoreCommand();
    void run() override;
    ref<Store> getStore();
    virtual ref<Store> createStore();
    /**
     * Main entry point, with a `Store` provided
     */
    virtual void run(ref<Store>) = 0;

private:
    std::shared_ptr<Store> _store;
};

/**
 * A command that copies something between `--from` and `--to` \ref
 * Store stores.
 */
struct CopyCommand : virtual StoreCommand
{
    std::string srcUri, dstUri;

    CopyCommand();

    ref<Store> createStore() override;

    ref<Store> getDstStore();
};

/**
 * A command that needs to evaluate Nix language expressions.
 */
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

/**
 * A mixin class for commands that process flakes, adding a few standard
 * flake-related options/flags.
 */
struct MixFlakeOptions : virtual Args, EvalCommand
{
    flake::LockFlags lockFlags;

    MixFlakeOptions();

    /**
     * The completion for some of these flags depends on the flake(s) in
     * question.
     *
     * This method should be implemented to gather all flakerefs the
     * command is operating with (presumably specified via some other
     * arguments) so that the completions for these flags can use them.
     */
    virtual std::vector<FlakeRef> getFlakeRefsForCompletion()
    { return {}; }
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

    /**
     * Complete an installable from the given prefix.
     */
    void completeInstallable(AddCompletions & completions, std::string_view prefix);

    /**
     * Convenience wrapper around the underlying function to make setting the
     * callback easier.
     */
    CompleterClosure getCompleteInstallable();
};

/**
 * A mixin class for commands that need a read-only flag.
 *
 * What exactly is "read-only" is unspecified, but it will usually be
 * the \ref Store "Nix store".
 */
struct MixReadOnlyOption : virtual Args
{
    MixReadOnlyOption();
};

/**
 * Like InstallablesCommand but the installables are not loaded.
 *
 * This is needed by `CmdRepl` which wants to load (and reload) the
 * installables itself.
 */
struct RawInstallablesCommand : virtual Args, SourceExprCommand
{
    RawInstallablesCommand();

    virtual void run(ref<Store> store, std::vector<std::string> && rawInstallables) = 0;

    void run(ref<Store> store) override;

    virtual void applyDefaultInstallables(std::vector<std::string> & rawInstallables);

    bool readFromStdIn = false;

    std::vector<FlakeRef> getFlakeRefsForCompletion() override;

private:

    std::vector<std::string> rawInstallables;
};

/**
 * A command that operates on a list of "installables", which can be
 * store paths, attribute paths, Nix expressions, etc.
 */
struct InstallablesCommand : RawInstallablesCommand
{
    virtual void run(ref<Store> store, Installables && installables) = 0;

    void run(ref<Store> store, std::vector<std::string> && rawInstallables) override;
};

/**
 * A command that operates on exactly one "installable".
 */
struct InstallableCommand : virtual Args, SourceExprCommand
{
    InstallableCommand();

    virtual void run(ref<Store> store, ref<Installable> installable) = 0;

    void run(ref<Store> store) override;

    std::vector<FlakeRef> getFlakeRefsForCompletion() override;

private:

    std::string _installable{"."};
};

struct MixOperateOnOptions : virtual Args
{
    OperateOn operateOn = OperateOn::Output;

    MixOperateOnOptions();
};

/**
 * A command that operates on zero or more extant store paths.
 *
 * If the argument the user passes is a some sort of recipe for a path
 * not yet built, it must be built first.
 */
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

/**
 * A command that operates on exactly one store path.
 */
struct StorePathCommand : public StorePathsCommand
{
    virtual void run(ref<Store> store, const StorePath & storePath) = 0;

    void run(ref<Store> store, StorePaths && storePaths) override;
};

/**
 * A helper class for registering \ref Command commands globally.
 */
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

    /***
     * Modify global environ based on `ignoreEnvironment`, `keep`, and
     * `unset`. It's expected that exec will be called before this class
     * goes out of scope, otherwise `environ` will become invalid.
     */
    void setEnviron();
};

void completeFlakeInputPath(
    AddCompletions & completions,
    ref<EvalState> evalState,
    const std::vector<FlakeRef> & flakeRefs,
    std::string_view prefix);

void completeFlakeRef(AddCompletions & completions, ref<Store> store, std::string_view prefix);

void completeFlakeRefWithFragment(
    AddCompletions & completions,
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
