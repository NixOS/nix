#include "shared.hh"
#include "store-api.hh"

using namespace nix;

int main(int argc, char ** argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();
        auto gzip = false;
        auto toMode = true;
        auto includeOutputs = false;
        auto dryRun = false;
        auto useSubstitutes = false;
        auto sshHost = string{};
        auto storePaths = PathSet{};
        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-copy-closure");
            else if (*arg == "--version")
                printVersion("nix-copy-closure");
            else if (*arg == "--gzip" || *arg == "--bzip2" || *arg == "--xz") {
                if (*arg != "--gzip")
                    printMsg(lvlError, format("Warning: ‘%1%’ is not implemented, falling back to gzip") % *arg);
                gzip = true;
            } else if (*arg == "--from")
                toMode = false;
            else if (*arg == "--to")
                toMode = true;
            else if (*arg == "--include-outputs")
                includeOutputs = true;
            else if (*arg == "--show-progress")
                printMsg(lvlError, "Warning: ‘--show-progress’ is not implemented");
            else if (*arg == "--dry-run")
                dryRun = true;
            else if (*arg == "--use-substitutes" || *arg == "-s")
                useSubstitutes = true;
            else if (sshHost.empty())
                sshHost = *arg;
            else
                storePaths.insert(*arg);
            return true;
        });
        if (sshHost.empty())
            throw UsageError("no host name specified");

        auto remoteUri = "ssh://" + sshHost + (gzip ? "?compress=true" : "");
        auto to = toMode ? openStore(remoteUri) : openStore();
        auto from = toMode ? openStore() : openStore(remoteUri);
        if (includeOutputs) {
            auto newPaths = PathSet{};
            for (const auto & p : storePaths) {
                auto outputs = from->queryDerivationOutputs(p);
                newPaths.insert(outputs.begin(), outputs.end());
            }
            storePaths.insert(newPaths.begin(), newPaths.end());
        }
        copyPaths(from, to, Paths(storePaths.begin(), storePaths.end()), useSubstitutes);
    });
}
