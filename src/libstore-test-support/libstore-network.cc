#include "nix/store/tests/libstore-network.hh"
#include "nix/util/error.hh"
#include "nix/util/environment-variables.hh"

#ifdef __linux__
#  include "nix/util/file-system.hh"
#  include "nix/util/linux-namespaces.hh"
#  include <sched.h>
#  include <sys/ioctl.h>
#  include <net/if.h>
#  include <netinet/in.h>
#endif

namespace nix::testing {

bool networkTestsAvailable = false;

#ifdef __linux__

static void enterNetworkNamespace()
{
    auto uid = ::getuid();
    auto gid = ::getgid();

    if (::unshare(CLONE_NEWUSER | CLONE_NEWNET) == -1)
        throw SysError("setting up a private network namespace for tests");

    std::filesystem::path procSelf = "/proc/self";
    writeFile(procSelf / "setgroups", "deny");
    writeFile(procSelf / "uid_map", fmt("%d %d 1", uid, uid));
    writeFile(procSelf / "gid_map", fmt("%d %d 1", gid, gid));

    AutoCloseFD fd(::socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
    if (!fd)
        throw SysError("cannot open IP socket for loopback interface");

    struct ::ifreq ifr = {};
    strcpy(ifr.ifr_name, "lo");
    ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
    if (::ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1)
        throw SysError("cannot set loopback interface flags");
}

#endif

void setupNetworkTests()
try {
    networkTestsAvailable = getEnvOs(OS_STR("NIX_TEST_FORCE_NETWORK_TESTS")).has_value();

#ifdef __linux__
    if (!networkTestsAvailable && userNamespacesSupported()) {
        enterNetworkNamespace();
        networkTestsAvailable = true;
    }
#endif
} catch (SystemError & e) {
    /* Ignore any set up errors. */
}

} // namespace nix::testing
