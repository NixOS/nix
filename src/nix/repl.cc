#include "eval.hh"
#include "eval-settings.hh"
#include "config-global.hh"
#include "globals.hh"
#include "command.hh"
#include "installable-value.hh"
#include "repl.hh"
#include "processes.hh"
#include "self-exe.hh"

namespace nix {

void runNix(Path program, const Strings & args,
    const std::optional<std::string> & input = {})
{
    auto subprocessEnv = getEnv();
    subprocessEnv["NIX_CONFIG"] = globalConfig.toKeyValue();
    //isInteractive avoid grabling interactive commands
    runProgram2(RunOptions {
        .program = getNixBin(program).string(),
        .args = args,
        .environment = subprocessEnv,
        .input = input,
        .isInteractive = true,
    });

    return;
}

struct CmdRepl : RawInstallablesCommand
{
    CmdRepl() {
        evalSettings.pureEval = false;
    }

    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::vector<std::string> files;

    Strings getDefaultFlakeAttrPaths() override
    {
        return {""};
    }

    bool forceImpureByDefault() override
    {
        return true;
    }

    std::string description() override
    {
        return "start an interactive environment for evaluating Nix expressions";
    }

    std::string doc() override
    {
        return
          #include "repl.md"
          ;
    }

    void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override
    {
        if (rawInstallables.empty() && (file.has_value() || expr.has_value())) {
            rawInstallables.push_back(".");
        }
    }

    void run(ref<Store> store, std::vector<std::string> && rawInstallables) override
    {
        auto state = getEvalState();
        auto getValues = [&]()->AbstractNixRepl::AnnotatedValues{
            auto installables = parseInstallables(store, rawInstallables);
            AbstractNixRepl::AnnotatedValues values;
            for (auto & installable_: installables){
                auto & installable = InstallableValue::require(*installable_);
                auto what = installable.what();
                if (file){
                    auto [val, pos] = installable.toValue(*state);
                    auto what = installable.what();
                    state->forceValue(*val, pos);
                    auto autoArgs = getAutoArgs(*state);
                    auto valPost = state->allocValue();
                    state->autoCallFunction(*autoArgs, *val, *valPost);
                    state->forceValue(*valPost, pos);
                    values.push_back( {valPost, what });
                } else {
                    auto [val, pos] = installable.toValue(*state);
                    values.push_back( {val, what} );
                }
            }
            return values;
        };
        auto repl = AbstractNixRepl::create(
            lookupPath,
            openStore(),
            state,
            getValues,
            runNix
        );
        repl->autoArgs = getAutoArgs(*repl->state);
        repl->initEnv();
        repl->mainLoop();
    }
};

static auto rCmdRepl = registerCommand<CmdRepl>("repl");

}
