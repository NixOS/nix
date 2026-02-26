#include "nix/main/shared.hh"
#include "nix/store/realisation.hh"
#include "nix/store/legacy-ssh-store.hh"
#include "nix/store/store-open.hh"
#include "nix/cmd/legacy.hh"
#include "man-pages.hh"

using namespace nix;

static int main_nix_copy_closure(int argc, char ** argv)
{
    {
        auto gzip = false;
        auto toMode = true;
        auto includeOutputs = false;
        auto dryRun = false;
        auto useSubstitutes = NoSubstitute;
        std::string sshHost;
        StringSet storePaths;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-copy-closure");
            else if (*arg == "--version")
                printVersion("nix-copy-closure");
            else if (*arg == "--gzip" || *arg == "--bzip2" || *arg == "--xz") {
                if (*arg != "--gzip")
                    warn("'%1%' is not implemented, falling back to gzip", *arg);
                gzip = true;
            } else if (*arg == "--from")
                toMode = false;
            else if (*arg == "--to")
                toMode = true;
            else if (*arg == "--include-outputs")
                includeOutputs = true;
            else if (*arg == "--show-progress")
                printMsg(lvlError, "Warning: '--show-progress' is not implemented");
            else if (*arg == "--dry-run")
                dryRun = true;
            else if (*arg == "--use-substitutes" || *arg == "-s")
                useSubstitutes = Substitute;
            else if (sshHost.empty())
                sshHost = *arg;
            else
                storePaths.insert(*arg);
            return true;
        });

        if (sshHost.empty())
            throw UsageError("no host name specified");

        auto remoteConfig =
            /* FIXME: This doesn't go through the back-compat machinery for IPv6 unbracketed URLs that
               is in StoreReference::parse. TODO: Maybe add a authority parsing function specifically
               for SSH reference parsing? */
            make_ref<LegacySSHStoreConfig>(ParsedURL::Authority::parse(sshHost), LegacySSHStoreConfig::Params{});
        remoteConfig->compress |= gzip;
        auto to = toMode ? remoteConfig->openStore() : openStore();
        auto from = toMode ? openStore() : remoteConfig->openStore();

        RealisedPath::Set storePaths2;
        for (auto & path : storePaths)
            storePaths2.insert(from->followLinksToStorePath(path));

        copyClosure(*from, *to, storePaths2, NoRepair, NoCheckSigs, useSubstitutes);

        return 0;
    }
}

static RegisterLegacyCommand r_nix_copy_closure("nix-copy-closure", main_nix_copy_closure);
