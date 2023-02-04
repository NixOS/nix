#include "eval.hh"
#include "globals.hh"
#include "command.hh"
#include "repl.hh"

namespace nix {

struct CmdRepl : RawInstallablesCommand
{
    CmdRepl() {
        evalSettings.pureEval = false;
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
        if (!settings.isExperimentalFeatureEnabled(Xp::ReplFlake) && !(file) && rawInstallables.size() >= 1) {
            warn("future versions of Nix will require using `--file` to load a file");
            if (rawInstallables.size() > 1)
                warn("more than one input file is not currently supported");
            auto filePath = rawInstallables[0].data();
            file = std::optional(filePath);
            rawInstallables.front() = rawInstallables.back();
            rawInstallables.pop_back();
        }
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
            for (auto & installable: installables){
                auto what = installable->what();
                if (file){
                    auto [val, pos] = installable->toValue(*state);
                    auto what = installable->what();
                    state->forceValue(*val, pos);
                    auto autoArgs = getAutoArgs(*state);
                    auto valPost = state->allocValue();
                    state->autoCallFunction(*autoArgs, *val, *valPost);
                    state->forceValue(*valPost, pos);
                    values.push_back( {valPost, what });
                } else {
                    auto [val, pos] = installable->toValue(*state);
                    values.push_back( {val, what} );
                }
            }
            return values;
        };
        auto repl = AbstractNixRepl::create(
            searchPath,
            openStore(),
            state,
            getValues
        );
        repl->autoArgs = getAutoArgs(*repl->state);
        repl->initEnv();
        repl->mainLoop();
    }
};

static auto rCmdRepl = registerCommand<CmdRepl>("repl");

}
