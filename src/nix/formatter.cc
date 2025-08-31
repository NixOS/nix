#include "nix/cmd/command.hh"
#include "nix/cmd/installable-flake.hh"
#include "nix/cmd/installable-value.hh"
#include "nix/expr/eval.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/cmd/installable-derived-path.hh"
#include "nix/util/environment-variables.hh"
#include "nix/store/globals.hh"

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

/** Common implementation bits for the `nix formatter` subcommands. */
struct MixFormatter : SourceExprCommand
{
    Strings getDefaultFlakeAttrPaths() override
    {
        return Strings{"formatter." + settings.thisSystem.get()};
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        return Strings{};
    }
};

struct CmdFormatterRun : MixFormatter, MixJSON
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

    void run(ref<Store> store) override
    {
        auto evalState = getEvalState();
        auto evalStore = getEvalStore();

        auto installable_ = parseInstallable(store, ".").cast<InstallableFlake>();
        auto & installable = InstallableValue::require(*installable_);
        auto app = installable.toApp(*evalState).resolve(evalStore, store);

        auto maybeFlakeDir = installable_->flakeRef.input.getSourcePath();
        assert(maybeFlakeDir.has_value());
        auto flakeDir = maybeFlakeDir.value();

        Strings programArgs{app.program};

        // Propagate arguments from the CLI
        for (auto & i : args) {
            programArgs.push_back(i);
        }

        // Add the path to the flake as an environment variable. This enables formatters to format the entire flake even
        // if run from a subdirectory.
        StringMap env = getEnv();
        env["PRJ_ROOT"] = flakeDir.string();

        // Release our references to eval caches to ensure they are persisted to disk, because
        // we are about to exec out of this process without running C++ destructors.
        evalState->evalCaches.clear();

        execProgramInStore(
            store,
            UseLookupPath::DontUse,
            app.program,
            programArgs,
            std::nullopt, // Use default system
            env);
    };
};

static auto rFormatterRun = registerCommand2<CmdFormatterRun>({"formatter", "run"});

struct CmdFormatterBuild : MixFormatter, MixOutLinkByDefault
{
    CmdFormatterBuild() {}

    std::string description() override
    {
        return "build the current flake's formatter";
    }

    std::string doc() override
    {
        return
#include "formatter-build.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    void run(ref<Store> store) override
    {
        auto evalState = getEvalState();
        auto evalStore = getEvalStore();

        auto installable_ = parseInstallable(store, ".");
        auto & installable = InstallableValue::require(*installable_);
        auto unresolvedApp = installable.toApp(*evalState);
        auto app = unresolvedApp.resolve(evalStore, store);
        auto buildables = unresolvedApp.build(evalStore, store);
        createOutLinksMaybe(buildables, store);

        logger->cout("%s", app.program);
    };
};

static auto rFormatterBuild = registerCommand2<CmdFormatterBuild>({"formatter", "build"});

struct CmdFmt : CmdFormatterRun
{
    void run(ref<Store> store) override
    {
        CmdFormatterRun::run(store);
    }
};

static auto rFmt = registerCommand<CmdFmt>("fmt");
