#include "command.hh"
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

    void run(ref<Store> store) {
        auto evalState = getEvalState();
        auto evalStore = getEvalStore();

        auto installable = parseInstallable(store, ".");
        auto app = installable->toApp(*evalState).resolve(evalStore, store);

        Strings programArgs{app.program};

        // Propagate arguments from the CLI
        if (args.empty()) {
            // Format the current flake out of the box
            programArgs.push_back(".");
        } else {
            // User wants more power, let them decide which paths to include/exclude
            for (auto &i : args) {
                programArgs.push_back(i);
            }
        }

        runProgramInStore(store, app.program, programArgs);
    };
};

static auto r2 = registerCommand<CmdFmt>("fmt");
