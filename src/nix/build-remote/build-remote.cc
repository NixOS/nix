#include <cstdlib>

#include "nix/store/build/build-remote.hh"
#include "nix/main/shared.hh"
#include "nix/main/plugin.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/util/serialise.hh"
#include "nix/util/util.hh"
#include "nix/cmd/legacy.hh"

namespace nix {

static int main_build_remote(int argc, char ** argv)
{
    {
        /* Upon exiting, Nix will attempt to terminate this process with
           SIGTERM. initNix will block or handle SIGTERM, so we need to unblock
           and unhandle it here.
        */
        struct sigaction act;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        act.sa_handler = SIG_DFL;
        if (sigaction(SIGTERM, &act, 0))
            throw SysError("resetting SIGTERM");

        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGTERM);
        if (pthread_sigmask(SIG_UNBLOCK, &set, nullptr))
            throw SysError("unblocking SIGTERM");

        logger = makeJSONLogger(getStandardError()).release();

        /* Ensure we don't get any SSH passphrase or host key popups. */
        unsetenv("DISPLAY");
        unsetenv("SSH_ASKPASS");

        /* If we ever use the common args framework, make sure to
           remove initPlugins below and initialize settings first.
        */
        if (argc != 2)
            throw UsageError("called without required arguments");

        auto rawVerbosity = string2Int<unsigned>(argv[1]);
        if (!rawVerbosity)
            throw UsageError("invalid verbosity '%s'", argv[1]);
        verbosity = verbosityFromIntClamped(*rawVerbosity);

        FdSource source(STDIN_FILENO);

        /* Read the parent's settings. */
        while (readInt(source)) {
            auto name = readString(source);
            auto value = readString(source);
            settings.set(name, value);
        }

        unsigned int maxBuildJobs = settings.getWorkerSettings().maxBuildJobs.get();
        settings.getWorkerSettings().maxBuildJobs.set("1"); // hack to make tests with local?root= work

        initPlugins();

        auto store = openStore();

        serveBuildHook(store, maxBuildJobs, source, STDERR_FILENO, 5);

        return 0;
    }
}

static RegisterLegacyCommand r_build_remote("build-remote", main_build_remote);

} // namespace nix
