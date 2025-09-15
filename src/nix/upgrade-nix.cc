#include "nix/util/processes.hh"
#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/store-api.hh"
#include "nix/store/filetransfer.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/attr-path.hh"
#include "nix/store/names.hh"
#include "nix/util/executable-path.hh"
#include "nix/store/globals.hh"
#include "self-exe.hh"

using namespace nix;

struct CmdUpgradeNix : MixDryRun, StoreCommand
{
    std::filesystem::path profileDir;

    CmdUpgradeNix()
    {
        addFlag({
            .longName = "profile",
            .shortName = 'p',
            .description = "The path to the Nix profile to upgrade.",
            .labels = {"profile-dir"},
            .handler = {&profileDir},
        });

        addFlag({
            .longName = "nix-store-paths-url",
            .description = "The URL of the file that contains the store paths of the latest Nix release.",
            .labels = {"url"},
            .handler = {&(std::string &) settings.upgradeNixStorePathUrl},
        });
    }

    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::string description() override
    {
        return "upgrade Nix to the latest stable version";
    }

    std::string doc() override
    {
        return
#include "upgrade-nix.md"
            ;
    }

    Category category() override
    {
        return catNixInstallation;
    }

    void run(ref<Store> store) override
    {
        evalSettings.pureEval = true;

        if (profileDir == "")
            profileDir = getProfileDir(store);

        printInfo("upgrading Nix in profile %s", profileDir);

        auto storePath = getLatestNix(store);

        auto version = DrvName(storePath.name()).version;

        if (dryRun) {
            logger->stop();
            warn("would upgrade to version %s", version);
            return;
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("downloading '%s'...", store->printStorePath(storePath)));
            store->ensurePath(storePath);
        }

        {
            Activity act(
                *logger, lvlInfo, actUnknown, fmt("verifying that '%s' works...", store->printStorePath(storePath)));
            auto program = store->printStorePath(storePath) + "/bin/nix-env";
            auto s = runProgram(program, false, {"--version"});
            if (s.find("Nix") == std::string::npos)
                throw Error("could not verify that '%s' works", program);
        }

        logger->stop();

        {
            Activity act(
                *logger,
                lvlInfo,
                actUnknown,
                fmt("installing '%s' into profile %s...", store->printStorePath(storePath), profileDir));

            // FIXME: don't call an external process.
            runProgram(
                getNixBin("nix-env").string(),
                false,
                {"--profile", profileDir.string(), "-i", store->printStorePath(storePath), "--no-sandbox"});
        }

        printInfo(ANSI_GREEN "upgrade to version %s done" ANSI_NORMAL, version);
    }

    /* Return the profile in which Nix is installed. */
    std::filesystem::path getProfileDir(ref<Store> store)
    {
        auto whereOpt = ExecutablePath::load().findName(OS_STR("nix-env"));
        if (!whereOpt)
            throw Error("couldn't figure out how Nix is installed, so I can't upgrade it");
        const auto & where = whereOpt->parent_path();

        printInfo("found Nix in %s", where);

        if (hasPrefix(where.string(), "/run/current-system"))
            throw Error("Nix on NixOS must be upgraded via 'nixos-rebuild'");

        auto profileDir = where.parent_path();

        // Resolve profile to /nix/var/nix/profiles/<name> link.
        while (canonPath(profileDir.string()).find("/profiles/") == std::string::npos
               && std::filesystem::is_symlink(profileDir))
            profileDir = readLink(profileDir.string());

        printInfo("found profile %s", profileDir);

        Path userEnv = canonPath(profileDir.string(), true);

        if (std::filesystem::exists(profileDir / "manifest.json"))
            throw Error(
                "directory %s is managed by 'nix profile' and currently cannot be upgraded by 'nix upgrade-nix'",
                profileDir);

        if (!std::filesystem::exists(profileDir / "manifest.nix"))
            throw Error("directory %s does not appear to be part of a Nix profile", profileDir);

        if (!store->isValidPath(store->parseStorePath(userEnv)))
            throw Error("directory '%s' is not in the Nix store", userEnv);

        return profileDir;
    }

    /* Return the store path of the latest stable Nix. */
    StorePath getLatestNix(ref<Store> store)
    {
        Activity act(*logger, lvlInfo, actUnknown, "querying latest Nix version");

        // FIXME: use nixos.org?
        auto req = FileTransferRequest(parseURL(settings.upgradeNixStorePathUrl.get()));
        auto res = getFileTransfer()->download(req);

        auto state = std::make_unique<EvalState>(LookupPath{}, store, fetchSettings, evalSettings);
        auto v = state->allocValue();
        state->eval(state->parseExprFromString(res.data, state->rootPath(CanonPath("/no-such-path"))), *v);
        Bindings & bindings = Bindings::emptyBindings;
        auto v2 = findAlongAttrPath(*state, settings.thisSystem, bindings, *v).first;

        return store->parseStorePath(
            state->forceString(*v2, noPos, "while evaluating the path tho latest nix version"));
    }
};

static auto rCmdUpgradeNix = registerCommand<CmdUpgradeNix>("upgrade-nix");
