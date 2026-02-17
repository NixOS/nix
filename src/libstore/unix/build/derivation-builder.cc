#include "nix/store/build/derivation-builder.hh"
#include "nix/util/file-system.hh"
#include "nix/store/local-store.hh"
#include "nix/util/processes.hh"
#include "nix/store/builtins.hh"
#include "nix/store/path-references.hh"
#include "nix/util/util.hh"
#include "nix/util/archive.hh"
#include "nix/util/git.hh"
#include "nix/store/daemon.hh"
#include "nix/util/topo-sort.hh"
#include "nix/store/build/child.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/globals.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/util/terminal.hh"
#include "nix/store/filetransfer.hh"

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#ifdef __linux__
#  include <sys/prctl.h>
#  include "nix/util/linux-namespaces.hh"
#endif

#include "store-config-private.hh"

#if HAVE_STATVFS
#  include <sys/statvfs.h>
#endif

#include <pwd.h>
#include <grp.h>
#include <iostream>

#include "nix/util/strings.hh"
#include "nix/util/signals.hh"

#include "store-config-private.hh"
#include "build/derivation-check.hh"

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#  include "nix/store/s3-url.hh"
#  include "nix/util/url.hh"
#endif

namespace nix {

void preserveDeathSignal(std::function<void()> fn)
{
#ifdef __linux__
    /* Record the old parent pid. This is to avoid a race in case the parent
       gets killed after setuid, but before we restored the death signal. It is
       zero if the parent isn't visible inside the PID namespace.
       See: https://stackoverflow.com/questions/284325/how-to-make-child-process-die-after-parent-exits */
    auto parentPid = getppid();

    int oldDeathSignal;
    if (prctl(PR_GET_PDEATHSIG, &oldDeathSignal) == -1)
        throw SysError("getting death signal");

    fn(); /* Invoke the callback that does setuid etc. */

    /* Set the old death signal. SIGKILL is set by default in startProcess,
       but it gets cleared after setuid. Without this we end up with runaway
       build processes if we get killed. */
    if (prctl(PR_SET_PDEATHSIG, oldDeathSignal) == -1)
        throw SysError("setting death signal");

    /* The parent got killed and we got reparented. Commit seppuku. This check
       doesn't help much with PID namespaces, but it's still useful without
       sandboxing. */
    if (oldDeathSignal && getppid() != parentPid)
        raise(oldDeathSignal);
#else
    fn(); /* Just call the function on non-Linux. */
#endif
}
} // namespace nix

#include "linux-chroot-derivation-builder.hh"
#include "linux-derivation-builder.hh"
#include "darwin-derivation-builder.hh"
#include "external-derivation-builder.hh"
#include "generic-unix-derivation-builder.hh"

namespace nix {

void DerivationBuilderDeleter::operator()(DerivationBuilder * builder) noexcept
{
    if (!builder) /* Idempotent and handles nullptr as any deleter must. */
        return;

    /* Note that this might call into virtual functions, which we can't do in a destructor of
       the DerivationBuilderImpl itself. */
    builder->cleanupOnDestruction();

    delete builder;
}

std::unique_ptr<DerivationBuilder, DerivationBuilderDeleter> makeDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
{
    bool useSandbox = false;
    const LocalSettings & localSettings = store.config->getLocalSettings();

    /* Are we doing a sandboxed build? */
    {
        if (localSettings.sandboxMode == smEnabled) {
            if (params.drvOptions.noChroot)
                throw Error(
                    "derivation '%s' has '__noChroot' set, "
                    "but that's not allowed when 'sandbox' is 'true'",
                    store.printStorePath(params.drvPath));
#ifdef __APPLE__
            if (params.drvOptions.additionalSandboxProfile != "")
                throw Error(
                    "derivation '%s' specifies a sandbox profile, "
                    "but this is only allowed when 'sandbox' is 'relaxed'",
                    store.printStorePath(params.drvPath));
#endif
            useSandbox = true;
        } else if (localSettings.sandboxMode == smDisabled)
            useSandbox = false;
        else if (localSettings.sandboxMode == smRelaxed)
            // FIXME: cache derivationType
            useSandbox = params.drv.type().isSandboxed() && !params.drvOptions.noChroot;
    }

    if (store.storeDir != store.config->realStoreDir.get()) {
#ifdef __linux__
        useSandbox = true;
#else
        throw Error("building using a diverted store is not supported on this platform");
#endif
    }

#ifdef __linux__
    if (useSandbox && !mountAndPidNamespacesSupported()) {
        if (!localSettings.sandboxFallback)
            throw Error(
                "this system does not support the kernel namespaces that are required for sandboxing; use '--no-sandbox' to disable sandboxing");
        debug("auto-disabling sandboxing because the prerequisite namespaces are not available");
        useSandbox = false;
    }

#endif

    if (!useSandbox && params.drvOptions.useUidRange(params.drv))
        throw Error("feature 'uid-range' is only supported in sandboxed builds");

#ifdef __APPLE__
    return makeDarwinDerivationBuilder(store, std::move(miscMethods), std::move(params), useSandbox);
#elif defined(__linux__)
    if (useSandbox)
        return makeLinuxChrootDerivationBuilder(store, std::move(miscMethods), std::move(params));

    return makeLinuxDerivationBuilder(store, std::move(miscMethods), std::move(params));
#else
    if (useSandbox)
        throw Error("sandboxing builds is not supported on this platform");

    return makeGenericUnixDerivationBuilder(store, std::move(miscMethods), std::move(params));
#endif
}

} // namespace nix
