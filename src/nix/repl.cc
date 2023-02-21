#include "nix/expr/eval.hh"
#include "nix/store/globals.hh"
#include "nix/cmd/command.hh"
#include "nix/cmd/repl.hh"

namespace nix {

struct CmdRepl : InstallablesCommand
{
    CmdRepl() {
        evalSettings.pureEval = false;
    }

    void prepare() override
    {
        if (!settings.isExperimentalFeatureEnabled(Xp::ReplFlake) && !(file) && this->_installables.size() >= 1) {
            warn("future versions of Nix will require using `--file` to load a file");
            if (this->_installables.size() > 1)
                warn("more than one input file is not currently supported");
            auto filePath = this->_installables[0].data();
            file = std::optional(filePath);
            _installables.front() = _installables.back();
            _installables.pop_back();
        }
        installables = InstallablesCommand::load();
    }

    std::vector<std::string> files;

    Strings getDefaultFlakeAttrPaths() override
    {
        return {""};
    }

    bool useDefaultInstallables() override
    {
        return file.has_value() or expr.has_value();
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

    void run(ref<Store> store) override
    {
        auto state = getEvalState();
        auto getValues = [&]()->AbstractNixRepl::AnnotatedValues{
            auto installables = load();
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
