#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/util/config-global.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/cmd/command.hh"
#include "nix/cmd/installable-value.hh"
#include "nix/cmd/repl.hh"
#include "nix/util/os-string.hh"
#include "nix/util/processes.hh"
#include "nix/util/environment-variables.hh"
#include "self-exe.hh"

namespace nix {

void runNix(const std::string & program, OsStrings args, const std::optional<std::string> & input = {})
{
    auto subprocessEnv = getEnvOs();
    subprocessEnv[OS_STR("NIX_CONFIG")] = string_to_os_string(globalConfig.toKeyValue());
    // isInteractive avoid grabling interactive commands
    runProgram2(
        RunOptions{
            .program = getNixBin(program).string(),
            .args = std::move(args),
            .environment = subprocessEnv,
            .input = input,
            .isInteractive = true,
        });

    return;
}

struct CmdRepl : RawInstallablesCommand
{
    CmdRepl()
    {
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
        auto getValues = [&]() -> AbstractNixRepl::AnnotatedValues {
            auto installables = parseInstallables(store, rawInstallables);
            AbstractNixRepl::AnnotatedValues values;
            for (auto & installable_ : installables) {
                auto & installable = InstallableValue::require(*installable_);
                auto what = installable.what();
                if (file) {
                    auto [val, pos] = installable.toValue(*state);
                    auto what = installable.what();
                    state->forceValue(*val, pos);
                    auto autoArgs = getAutoArgs(*state);
                    auto valPost = state->allocValue();
                    state->autoCallFunction(*autoArgs, *val, *valPost);
                    state->forceValue(*valPost, pos);
                    values.push_back({valPost, what});
                } else {
                    auto [val, pos] = installable.toValue(*state);
                    values.push_back({val, what});
                }
            }
            return values;
        };
        auto repl = AbstractNixRepl::create(lookupPath, state, getValues, runNix);
        repl->autoArgs = getAutoArgs(*repl->state);
        repl->initEnv();
        repl->mainLoop();
    }
};

static auto rCmdRepl = registerCommand<CmdRepl>("repl");

} // namespace nix
