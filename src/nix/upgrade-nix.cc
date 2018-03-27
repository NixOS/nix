#include "command.hh"
#include "store-api.hh"
#include "download.hh"
#include "eval.hh"
#include "attr-path.hh"

using namespace nix;

struct CmdUpgradeNix : StoreCommand
{
    Path profileDir;

    CmdUpgradeNix()
    {
        mkFlag()
            .longName("profile")
            .shortName('p')
            .labels({"profile-dir"})
            .description("the Nix profile to upgrade")
            .dest(&profileDir);
    }

    std::string name() override
    {
        return "upgrade-nix";
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

    void run(ref<Store> store) override
    {
        evalSettings.pureEval = true;

        if (profileDir == "")
            profileDir = getProfileDir(store);

        printInfo("upgrading Nix in profile '%s'", profileDir);

        Path storePath;
        {
            Activity act(*logger, lvlInfo, actUnknown, "querying latest Nix version");
            storePath = getLatestNix(store);
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("downloading '%s'...", storePath));
            store->ensurePath(storePath);
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("verifying that '%s' works...", storePath));
            auto program = storePath + "/bin/nix-env";
            auto s = runProgram(program, false, {"--version"});
            if (s.find("Nix") == std::string::npos)
                throw Error("could not verify that '%s' works", program);
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("installing '%s' into profile '%s'...", storePath, profileDir));
            runProgram(settings.nixBinDir + "/nix-env", false,
                {"--profile", profileDir, "-i", storePath, "--no-sandbox"});
        }
    }

    /* Return the profile in which Nix is installed. */
    Path getProfileDir(ref<Store> store)
    {
        Path where;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH"), ":"))
            if (pathExists(dir + "/nix-env")) {
                where = dir;
                break;
            }

        if (where == "")
            throw Error("couldn't figure out how Nix is installed, so I can't upgrade it");

        printInfo("found Nix in '%s'", where);

        if (hasPrefix(where, "/run/current-system"))
            throw Error("Nix on NixOS must be upgraded via 'nixos-rebuild'");

        Path profileDir;
        Path userEnv;

        if (baseNameOf(where) != "bin" ||
            !hasSuffix(userEnv = canonPath(profileDir = dirOf(where), true), "user-environment"))
            throw Error("directory '%s' does not appear to be part of a Nix profile", where);

        if (!store->isValidPath(userEnv))
            throw Error("directory '%s' is not in the Nix store", userEnv);

        return profileDir;
    }

    /* Return the store path of the latest stable Nix. */
    Path getLatestNix(ref<Store> store)
    {
        // FIXME: use nixos.org?
        auto req = DownloadRequest("https://github.com/NixOS/nixpkgs/raw/master/nixos/modules/installer/tools/nix-fallback-paths.nix");
        auto res = getDownloader()->download(req);

        EvalState state(Strings(), store);
        auto v = state.allocValue();
        state.eval(state.parseExprFromString(*res.data, "/no-such-path"), *v);
        Bindings & bindings(*state.allocBindings(0));
        auto v2 = findAlongAttrPath(state, settings.thisSystem, bindings, *v);

        return state.forceString(*v2);
    }
};

static RegisterCommand r1(make_ref<CmdUpgradeNix>());
