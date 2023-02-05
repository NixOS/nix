#pragma once
///@file

#include "store-installables.hh"
#include "args.hh"
#include "path.hh"

#include <optional>

namespace nix {

extern std::string programPath;

extern char * * savedArgv;

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

struct HasStore
{
    ref<Store> getStore();
    virtual ref<Store> createStore();

private:
    std::shared_ptr<Store> _store;
};

/**
 * A command that requires a \ref Store "Nix store".
 */
struct StoreCommand : virtual Command, virtual HasStore
{
    /**
     * Main entry point, with a Store provided
     */
    void run() override;
    virtual void run(ref<Store>) = 0;
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

struct HasDrvStore : virtual HasStore
{
    virtual ref<Store> getDrvStore();

protected:
    std::shared_ptr<Store> drvStore;
};

struct DrvCommand : virtual HasDrvStore, virtual StoreCommand
{
};

struct GetRawInstallables : virtual AbstractArgs
{
    /**
     * Get the unparsed installables allociated with this command
     *
     * This is needed for the completions of *other* arguments that
     * depends on these.
     *
     * @return A fresh vector, because the underlying command doesn't
     * always store a vector of raw installables.
     */
    virtual std::vector<std::string> getRawInstallables() = 0;
};

struct ParseInstallableArgs
{
    virtual Installables parseInstallables(
        ref<Store> store, std::vector<std::string> ss) = 0;

    virtual ref<Installable> parseInstallable(
        ref<Store> store, const std::string & installable) = 0;

    /**
     * Complete an installable from the given prefix.
     */
    virtual void completeInstallable(AddCompletions & completions, std::string_view prefix)
    { };

    /**
     * Convenience wrapper around the underlying function to make setting the
     * callback easier.
     */
    AbstractArgs::CompleterClosure getCompleteInstallable();

    // FIXME make const after CmdRepl's override is fixed up
    virtual void applyDefaultInstallables(std::vector<std::string> & rawInstallables) = 0;

    typedef ref<ParseInstallableArgs> MakeDefaultFun(GetRawInstallables &);

    static MakeDefaultFun * makeDefault;

    struct RegisterDefault
    {
        RegisterDefault(MakeDefaultFun);
    };
};

struct MixDefaultParseInstallableArgs : virtual ParseInstallableArgs
{
    ref<ParseInstallableArgs> def;

    MixDefaultParseInstallableArgs(GetRawInstallables & args)
        : def(ParseInstallableArgs::makeDefault(args))
    { }

    Installables parseInstallables(
        ref<Store> store, std::vector<std::string> ss) override
    {
        return def->parseInstallables(store, std::move(ss));
    }

    ref<Installable> parseInstallable(
        ref<Store> store, const std::string & installable) override
    {
        return def->parseInstallable(store, installable);
    }

    void completeInstallable(AddCompletions & completions, std::string_view prefix) override
    {
        return def->completeInstallable(completions, prefix);
    }

    virtual void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override
    {
        return def->applyDefaultInstallables(rawInstallables);
    }
};

/**
 * Like InstallablesCommand but the installables are not loaded.
 *
 * This is needed by `CmdRepl` which wants to load (and reload) the
 * installables itself.
 */
struct RawInstallablesCommand : virtual DrvCommand, virtual GetRawInstallables, virtual ParseInstallableArgs
{
    RawInstallablesCommand();

    virtual void run(ref<Store> store, std::vector<std::string> && rawInstallables) = 0;

    void run(ref<Store> store) override;

    bool readFromStdIn = false;

    std::vector<std::string> getRawInstallables() override;

private:

    std::vector<std::string> rawInstallables;
};

/**
 * A command that operates on a list of "installables", which can be
 * store paths, attribute paths, Nix expressions, etc.
 */
struct AbstractInstallablesCommand : virtual RawInstallablesCommand
{
    virtual void run(ref<Store> store, Installables && installables) = 0;

    void run(ref<Store> store, std::vector<std::string> && rawInstallables) override;
};

/**
 * A command that operates on exactly one "installable".
 */
struct InstallablesCommand : AbstractInstallablesCommand, MixDefaultParseInstallableArgs
{
    InstallablesCommand()
        : MixDefaultParseInstallableArgs(static_cast<GetRawInstallables &>(*this))
    { }
};

/* A core command that operates on exactly one "installable" */
struct AbstractInstallableCommand : virtual DrvCommand, virtual GetRawInstallables, virtual ParseInstallableArgs
{
    AbstractInstallableCommand();

    virtual void run(ref<Store> store, ref<Installable> installable) = 0;

    void run(ref<Store> store) override;

    std::vector<std::string> getRawInstallables() override final;

protected:

    std::string _installable{"."};
};

struct InstallableCommand : AbstractInstallableCommand, MixDefaultParseInstallableArgs
{
    InstallableCommand()
        : MixDefaultParseInstallableArgs(static_cast<GetRawInstallables &>(*this))
    { }
};

struct MixOperateOnOptions
{
    OperateOn operateOn = OperateOn::Output;

    MixOperateOnOptions(AbstractArgs & args);
};

/**
 * A command that operates on zero or more extant store paths.
 *
 * If the argument the user passes is a some sort of recipe for a path
 * not yet built, it must be built first.
 */
struct BuiltPathsCommand : InstallablesCommand, MixOperateOnOptions
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

std::string showVersions(const std::set<std::string> & versions);

void printClosureDiff(
    ref<Store> store,
    const StorePath & beforePath,
    const StorePath & afterPath,
    std::string_view indent);

}
