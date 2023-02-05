#pragma once
///@file

#include "installable-value.hh"
#include "store-command.hh"
#include "common-eval-args.hh"
#include "flake/lockfile.hh"

namespace nix {

class EvalState;
struct Pos;

struct HasEvalState : virtual HasDrvStore, MixEvalArgs
{
    bool startReplOnEvalErrors = false;
    bool ignoreExceptionsDuringTry = false;

    HasEvalState(AbstractArgs & args);

    ~HasEvalState();

    ref<Store> getDrvStore() override;

    ref<EvalState> getEvalState();

private:

    std::shared_ptr<EvalState> evalState;
};

/**
 * A command that needs to evaluate Nix language expressions.
 */
struct EvalCommand : virtual DrvCommand, virtual HasEvalState
{
    EvalCommand()
        : MixRepair(static_cast<Command &>(*this))
        , HasEvalState(static_cast<DrvCommand &>(*this))
    { }
};

/**
 * A mixin class for commands that process flakes, adding a few standard
 * flake-related options/flags.
 */
struct MixFlakeOptions : virtual HasEvalState
{
    flake::LockFlags lockFlags;

    MixFlakeOptions(AbstractArgs & args);

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

/**
 * A class for parsing all installable, both low level
 * InstallableDerivedPath and high level InstallableValue and its
 * subclassses.
 */
struct ParseInstallableValueArgs : virtual ParseInstallableArgs, virtual MixFlakeOptions
{
    GetRawInstallables & args;

    ParseInstallableValueArgs(GetRawInstallables & args);

    std::optional<Path> file;
    std::optional<std::string> expr;

    virtual Strings getDefaultFlakeAttrPaths();

    virtual Strings getDefaultFlakeAttrPathPrefixes();

    Installables parseInstallables(
        ref<Store> store, std::vector<std::string> ss) override;

    ref<Installable> parseInstallable(
        ref<Store> store, const std::string & s) override;

    void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override;

    void completeInstallable(AddCompletions & completions, std::string_view prefix) override;

    std::vector<FlakeRef> getFlakeRefsForCompletion() override;
};

struct SourceExprCommand : virtual EvalCommand, ParseInstallableValueArgs, virtual GetRawInstallables
{
    SourceExprCommand()
        : MixRepair(static_cast<Command &>(*this))
        , HasEvalState(static_cast<DrvCommand &>(*this))
        , MixFlakeOptions(static_cast<EvalCommand &>(*this))
        , ParseInstallableValueArgs(static_cast<GetRawInstallables &>(*this))
    { }
};

/**
 * A mixin class for commands that need a read-only flag.
 *
 * What exactly is "read-only" is unspecified, but it will usually be
 * the \ref Store "Nix store".
 */
struct MixReadOnlyOption
{
    MixReadOnlyOption(AbstractArgs & args);
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

}
