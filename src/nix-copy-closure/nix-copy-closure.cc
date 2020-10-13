#include "shared.hh"
#include "store-api.hh"
#include "../nix/legacy.hh"

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
        PathSet storePaths;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-copy-closure");
            else if (*arg == "--version")
                printVersion("nix-copy-closure");
            else if (*arg == "--gzip" || *arg == "--bzip2" || *arg == "--xz") {
                if (*arg != "--gzip")
                    printMsg(lvlError, format("Warning: '%1%' is not implemented, falling back to gzip") % *arg);
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

        initPlugins();

        if (sshHost.empty())
            throw UsageError("no host name specified");

        auto remoteUri = "ssh://" + sshHost + (gzip ? "?compress=true" : "");
        auto to = toMode ? openStore(remoteUri) : openStore();
        auto from = toMode ? openStore() : openStore(remoteUri);

        StorePathSet storePaths2;
        for (auto & path : storePaths)
            storePaths2.insert(from->followLinksToStorePath(path));

        StorePathSet closure;
        from->computeFSClosure(storePaths2, closure, false, includeOutputs);

        copyPaths(from, to, closure, NoRepair, NoCheckSigs, useSubstitutes);

        from->sync();
        to->sync();

        return 0;
    }
}

static RegisterLegacyCommand r_nix_copy_closure("nix-copy-closure", main_nix_copy_closure);
