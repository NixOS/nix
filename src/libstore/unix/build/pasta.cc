#ifdef __linux__

#  include "pasta.hh"
#  include "nix/util/error.hh"
#  include "nix/util/file-system.hh"
#  include "nix/util/processes.hh"
#  include "nix/util/strings.hh"
#  include "nix/util/util.hh"

#  include "nix/util/fmt.hh"
#  include <regex>
#  include <sys/socket.h>
#  include <net/if.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <linux/capability.h>
#  include <netinet/in.h>
#  include <sys/types.h>

namespace nix {
namespace pasta {

Pid setupPasta(
    const Path & pastaPath,
    pid_t pid,
    std::optional<uid_t> buildUserId,
    std::optional<gid_t> buildGroupId,
    bool usingUserNamespace)
{
    // Bring up pasta for handling FOD networking. We don't let it daemonize
    // itself for process management reasons and kill it manually when done.

    Strings args = {
        "--quiet",        "--foreground",    "--config-net",     "--gateway",       PASTA_HOST_IPV4, "--address",
        PASTA_CHILD_IPV4, "--netmask",       PASTA_IPV4_NETMASK, "--dns-forward",   PASTA_HOST_IPV4, "--gateway",
        PASTA_HOST_IPV6,  "--address",       PASTA_CHILD_IPV6,   "--dns-forward",   PASTA_HOST_IPV6, "--ns-ifname",
        PASTA_NS_IFNAME,  "--no-netns-quit", "--netns",          "/proc/self/fd/0",
    };

    AutoCloseFD netns(open(fmt("/proc/%i/ns/net", pid).c_str(), O_RDONLY | O_CLOEXEC));
    if (!netns) {
        throw SysError("failed to open netns");
    }

    AutoCloseFD userns;
    if (usingUserNamespace) {
        userns = AutoCloseFD(open(fmt("/proc/%i/ns/user", pid).c_str(), O_RDONLY | O_CLOEXEC));
        if (!userns) {
            throw SysError("failed to open userns");
        }
        args.push_back("--userns");
        args.push_back("/proc/self/fd/1");
    }

    // FIXME ideally we want a notification when pasta exits, but we cannot do
    // this at present. Without such support we need to busy-wait for pasta to
    // set up the namespace completely and time out after a while for the case
    // of pasta launch failures. Pasta logs go to syslog only for now as well.

    // Use startProcess to launch pasta with proper file descriptor setup
    return startProcess([&]() {
        // Set up file descriptor redirections
        if (dup2(netns.get(), 0) == -1)
            throw SysError("cannot redirect netns fd to stdin");

        if (userns) {
            if (dup2(userns.get(), 1) == -1)
                throw SysError("cannot redirect userns fd to stdout");
        }

        // Set user/group if specified
        if (buildGroupId && setgid(*buildGroupId) == -1)
            throw SysError("setgid failed");
        if (buildUserId && setuid(*buildUserId) == -1)
            throw SysError("setuid failed");

        // Execute pasta
        Strings allArgs = {pastaPath};
        allArgs.insert(allArgs.end(), args.begin(), args.end());

        execvp(pastaPath.c_str(), stringsToCharPtrs(allArgs).data());
        throw SysError("executing pasta");
    });
}

void waitForPastaInterface()
{
    // Wait for the pasta interface to appear. pasta can't signal us when
    // it's done setting up the namespace, so we have to wait for a while
    AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
    if (!fd)
        throw SysError("cannot open IP socket");

    struct ifreq ifr;
    strcpy(ifr.ifr_name, PASTA_NS_IFNAME);

    // Wait two minutes for the interface to appear. If it does not do so
    // we are either grossly overloaded, or pasta startup failed somehow.
    static constexpr int SINGLE_WAIT_US = 1000;
    static constexpr int TOTAL_WAIT_US = 120'000'000;

    for (unsigned tries = 0;; tries++) {
        if (tries > TOTAL_WAIT_US / SINGLE_WAIT_US) {
            throw Error(
                "sandbox network setup timed out, please check daemon logs for "
                "possible error output.");
        } else if (ioctl(fd.get(), SIOCGIFFLAGS, &ifr) == 0) {
            if ((ifr.ifr_ifru.ifru_flags & IFF_UP) != 0) {
                break;
            }
        } else if (errno == ENODEV) {
            usleep(SINGLE_WAIT_US);
        } else {
            throw SysError("cannot get loopback interface flags");
        }
    }
}

std::string rewriteResolvConf(const std::string & fromHost)
{
    static constexpr auto flags = std::regex::ECMAScript | std::regex::multiline;
    static std::regex lineRegex("^nameserver\\s.*$", flags);
    static std::regex v4Regex("^nameserver\\s+\\d{1,3}\\.", flags);
    static std::regex v6Regex("^nameserver.*:", flags);

    std::string nsInSandbox = "\n";
    if (std::regex_search(fromHost, v4Regex)) {
        nsInSandbox += nix::fmt("nameserver %s\n", PASTA_HOST_IPV4);
    }
    if (std::regex_search(fromHost, v6Regex)) {
        nsInSandbox += nix::fmt("nameserver %s\n", PASTA_HOST_IPV6);
    }

    return std::regex_replace(fromHost, lineRegex, "") + nsInSandbox;
}

void killPasta(Pid & pastaPid)
{
    // FIXME we really want to send SIGTERM instead and wait for pasta to exit,
    // but we do not have the infra for that right now. We send SIGKILL instead
    // and treat exiting with that as a successful exit code until such a time.
    // This is not likely to cause problems since pasta runs as the build user,
    // but not inside the build sandbox. If it's killed it's either due to some
    // external influence (in which case the sandboxed child will probably fail
    // due to network errors, if it used the network at all) or some bug in nix
    if (auto status = pastaPid.kill(); !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
        if (WIFSIGNALED(status)) {
            throw Error("pasta killed by signal %i", WTERMSIG(status));
        } else if (WIFEXITED(status)) {
            throw Error("pasta exited with code %i", WEXITSTATUS(status));
        } else {
            throw Error("pasta exited with status %i", status);
        }
    }
}

} // namespace pasta
} // namespace nix

#endif // __linux__
