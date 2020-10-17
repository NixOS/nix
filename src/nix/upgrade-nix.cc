#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "filetransfer.hh"
#include "eval.hh"
#include "attr-path.hh"
#include "names.hh"
#include "progress-bar.hh"

using namespace nix;

struct CmdUpgradeNix : MixDryRun, StoreCommand
{
    Path profileDir;
    std::string storePathsUrl = "https://github.com/NixOS/nixpkgs/raw/master/nixos/modules/installer/tools/nix-fallback-paths.nix";

    CmdUpgradeNix()
    {
        addFlag({
            .longName = "profile",
            .shortName = 'p',
            .description = "the Nix profile to upgrade",
            .labels = {"profile-dir"},
            .handler = {&profileDir}
        });

        addFlag({
            .longName = "nix-store-paths-url",
            .description = "URL of the file that contains the store paths of the latest Nix release",
            .labels = {"url"},
            .handler = {&storePathsUrl}
        });
    }

    std::string description() override
    {
        return "upgrade Nix to the latest stable version";
    }

    Examples examples() override
    {
        return {
            Example{
                "To upgrade Nix to the latest stable version:",
                "nix upgrade-nix"
            },
            Example{
                "To upgrade Nix in a specific profile:",
                "nix upgrade-nix -p /nix/var/nix/profiles/per-user/alice/profile"
            },
        };
    }

    Category category() override { return catNixInstallation; }

    void run(ref<Store> store) override
    {
        evalSettings.pureEval = true;

        if (profileDir == "")
            profileDir = getProfileDir(store);

        printInfo("upgrading Nix in profile '%s'", profileDir);

        auto storePath = getLatestNix(store);

        auto version = DrvName(storePath.name()).version;

        if (dryRun) {
            stopProgressBar();
            logWarning({
                .name = "Version update",
                .hint = hintfmt("would upgrade to version %s", version)
            });
            return;
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("downloading '%s'...", store->printStorePath(storePath)));
            store->ensurePath(storePath);
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("verifying that '%s' works...", store->printStorePath(storePath)));
            auto program = store->printStorePath(storePath) + "/bin/nix-env";
            auto s = runProgram(program, false, {"--version"});
            if (s.find("Nix") == std::string::npos)
                throw Error("could not verify that '%s' works", program);
        }

        stopProgressBar();

        {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("installing '%s' into profile '%s'...", store->printStorePath(storePath), profileDir));
            runProgram(settings.nixBinDir + "/nix-env", false,
                {"--profile", profileDir, "-i", store->printStorePath(storePath), "--no-sandbox"});
        }

        printInfo(ANSI_GREEN "upgrade to version %s done" ANSI_NORMAL, version);
    }

    /* Return the profile in which Nix is installed. */
    Path getProfileDir(ref<Store> store)
    {
        Path where;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH").value_or(""), ":"))
            if (pathExists(dir + "/nix-env")) {
                where = dir;
                break;
            }

        if (where == "")
            throw Error("couldn't figure out how Nix is installed, so I can't upgrade it");

        printInfo("found Nix in '%s'", where);

        if (hasPrefix(where, "/run/current-system"))
            throw Error("Nix on NixOS must be upgraded via 'nixos-rebuild'");

        Path profileDir = dirOf(where);

        // Resolve profile to /nix/var/nix/profiles/<name> link.
        while (canonPath(profileDir).find("/profiles/") == std::string::npos && isLink(profileDir))
            profileDir = readLink(profileDir);

        printInfo("found profile '%s'", profileDir);

        Path userEnv = canonPath(profileDir, true);

        if (baseNameOf(where) != "bin" ||
            !hasSuffix(userEnv, "user-environment"))
            throw Error("directory '%s' does not appear to be part of a Nix profile", where);

        if (!store->isValidPath(store->parseStorePath(userEnv)))
            throw Error("directory '%s' is not in the Nix store", userEnv);

        return profileDir;
    }

    /* Return the store path of the latest stable Nix. */
    StorePath getLatestNix(ref<Store> store)
    {
        Activity act(*logger, lvlInfo, actUnknown, "querying latest Nix version");

        // FIXME: use nixos.org?
        auto req = FileTransferRequest(storePathsUrl);
        auto res = getFileTransfer()->download(req);

        auto state = std::make_unique<EvalState>(Strings(), store);
        auto v = state->allocValue();
        state->eval(state->parseExprFromString(*res.data, "/no-such-path"), *v);
        Bindings & bindings(*state->allocBindings(0));
        auto v2 = findAlongAttrPath(*state, settings.thisSystem, bindings, *v).first;

        return store->parseStorePath(state->forceString(*v2));
    }
};

static auto rCmdUpgradeNix = registerCommand<CmdUpgradeNix>("upgrade-nix");
