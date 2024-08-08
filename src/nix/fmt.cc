#include "command.hh"
#include "installable-value.hh"
#include "installable-flake.hh"
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

        Path nixfmt = "nixfmt";
        FlakeRef nixpkgs = defaultNixpkgsFlakeRef();
        nixpkgs = nixpkgs.resolve(evalStore);
        auto nixpkgsLockFlags = lockFlags;
        auto installable_ = parseInstallable(store, ".");

        // Check for "formatters.SYSTEM", too slow on Nixpkgs until lazy-trees
        /*
        try {
            if (auto * i = dynamic_cast<const InstallableFlake *>(&*installable_)) {
                auto & installable = InstallableFlake::require(*installable_);
                auto app = installable.toApp(*evalState).resolve(evalStore, store);
                nixfmt = app.program;
            }
        } catch (Error &) {
            // ignoreException();
        }
        */

        // Check for nixpkgs input, too slow on Nixpkgs until lazy-trees
        /*
        if (nixfmt == "nixfmt") {
            try {
                nixpkgsLockFlags.inputOverrides = {};
                nixpkgsLockFlags.inputUpdates = {};

                // Hard code the nixpkgs revision from <nixpkgs>/ci/pinned-nixpgs.json
                if (auto * i = dynamic_cast<const InstallableFlake *>(&*installable_))
                    nixpkgs = i->nixpkgsFlakeRef();
            } catch (Error &) {
                // ignoreException();
            }
        }
        */

        // Check for <nixpkgs>/ci/pinned-nixpkgs.json and resolve it
        if (nixfmt == "nixfmt") {
            try {
                auto res = nixpkgs.fetchTree(store);
                auto s = store->printStorePath(res.first) + "/ci/pinned-nixpkgs.json";
                if (pathExists(s)) {
                    nlohmann::json pinned_json = nlohmann::json::parse(readFile(s));
                    auto pinned_rev = getString(pinned_json["rev"]);
                    nixpkgs = FlakeRef::fromAttrs(fetchSettings, {{"type","indirect"}, {"id", "nixpkgs"},{"rev", pinned_rev}});
                    nixpkgs = nixpkgs.resolve(evalStore);
                }
            } catch (Error &) {
                // ignoreException();
            }
        }
        // Check for nixfmt, otherwise use PATH
        if (nixfmt == "nixfmt") {
            try {
                auto nixfmtInstallable = make_ref<InstallableFlake>(
                    this,
                    evalState,
                    std::move(nixpkgs),
                    "nixfmt-rfc-style",
                    ExtendedOutputsSpec::Default(),
                    Strings{},
                    Strings{"legacyPackages." + settings.thisSystem.get() + "."},
                    nixpkgsLockFlags);

                bool found = false;

                for (auto & path : Installable::toStorePathSet(getEvalStore(), store, Realise::Outputs, OperateOn::Output, {nixfmtInstallable})) {
                    auto s = store->printStorePath(path) + "/bin/nixfmt";
                    if (pathExists(s)) {
                        nixfmt = s;
                        found = true;
                        break;
                    }
                }

                if (!found)
                    throw Error("package 'nixpkgs#nixfmt-rfc-style' does not provide a 'bin/nixfmt'");

            } catch (Error &) {
                ignoreException();
            }
        }

        Strings programArgs{nixfmt};

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

        // Release our references to eval caches to ensure they are persisted to disk, because
        // we are about to exec out of this process without running C++ destructors.
        evalState->evalCaches.clear();

        execProgramInStore(store, UseLookupPath::DontUse, nixfmt, programArgs);
    };
};

static auto r2 = registerCommand<CmdFmt>("fmt");
