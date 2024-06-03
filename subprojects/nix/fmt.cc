#include "command.hh"
#include "installable-value.hh"
#include "eval.hh"
#include "run.hh"

using namespace nix;

struct CmdFmt : SourceExprCommand {
    std::vector<std::string> args;

    CmdFmt() { expectArgs({.label = "args", .handler = {&args}}); }

    std::string description() override {
        return "reformat your code in the standard style";
    }

    std::string doc() override {
        return
          #include "fmt.md"
          ;
    }

    Category category() override { return catSecondary; }

    Strings getDefaultFlakeAttrPaths() override {
        return Strings{"formatter." + settings.thisSystem.get()};
    }

    Strings getDefaultFlakeAttrPathPrefixes() override { return Strings{}; }

    void run(ref<Store> store) override
    {
        auto evalState = getEvalState();
        auto evalStore = getEvalStore();

        auto installable_ = parseInstallable(store, ".");
        auto & installable = InstallableValue::require(*installable_);
        auto app = installable.toApp(*evalState).resolve(evalStore, store);

        Strings programArgs{app.program};

        // Propagate arguments from the CLI
        for (auto &i : args) {
            programArgs.push_back(i);
        }

        // Release our references to eval caches to ensure they are persisted to disk, because
        // we are about to exec out of this process without running C++ destructors.
        evalState->evalCaches.clear();

        execProgramInStore(store, UseLookupPath::DontUse, app.program, programArgs);
    };
};

static auto r2 = registerCommand<CmdFmt>("fmt");
