#include "nix/cmd/command.hh"
#include "nix/cmd/installable-value.hh"
#include "nix/expr/eval.hh"
#include "run.hh"

using namespace nix;

struct CmdFormatter : NixMultiCommand
{
    CmdFormatter()
        : NixMultiCommand("formatter", RegisterCommand::getCommandsFor({"formatter"}))
    {
    }

    std::string description() override
    {
        return "build or run the formatter";
    }

    Category category() override
    {
        return catSecondary;
    }
};

static auto rCmdFormatter = registerCommand<CmdFormatter>("formatter");

struct CmdFormatterRun : SourceExprCommand
{
    std::vector<std::string> args;

    CmdFormatterRun()
    {
        expectArgs({.label = "args", .handler = {&args}});
    }

    std::string description() override
    {
        return "reformat your code in the standard style";
    }

    std::string doc() override
    {
        return
#include "formatter-run.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return Strings{"formatter." + settings.thisSystem.get()};
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        return Strings{};
    }

    void run(ref<Store> store) override
    {
        auto evalState = getEvalState();
        auto evalStore = getEvalStore();

        auto installable_ = parseInstallable(store, ".");
        auto & installable = InstallableValue::require(*installable_);
        auto app = installable.toApp(*evalState).resolve(evalStore, store);

        Strings programArgs{app.program};

        // Propagate arguments from the CLI
        for (auto & i : args) {
            programArgs.push_back(i);
        }

        // Release our references to eval caches to ensure they are persisted to disk, because
        // we are about to exec out of this process without running C++ destructors.
        evalState->evalCaches.clear();

        execProgramInStore(store, UseLookupPath::DontUse, app.program, programArgs);
    };
};

static auto rFormatterRun = registerCommand2<CmdFormatterRun>({"formatter", "run"});

struct CmdFmt : CmdFormatterRun
{
    void run(ref<Store> store) override
    {
        CmdFormatterRun::run(store);
    }
};

static auto rFmt = registerCommand<CmdFmt>("fmt");
