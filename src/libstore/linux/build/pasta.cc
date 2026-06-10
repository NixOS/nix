#include "nix/store/build/pasta.hh"
#include "nix/util/error.hh"
#include "nix/util/processes.hh"

#include "nix/util/fmt.hh"
#include <regex>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <netinet/in.h>
#include <sys/types.h>

namespace nix {
namespace pasta {

void setupPasta(
    const std::filesystem::path & pastaPath,
    pid_t pid,
    std::optional<uid_t> buildUserId,
    std::optional<gid_t> buildGroupId,
    bool usingUserNamespace)
{
    // Bring up pasta, for handling FOD networking. We don't let it daemonize
    // itself for process managements reasons and kill it manually when done.

    // TODO add a new sandbox mode flag to disable all or parts of this?
    Strings args = {
        // clang-format off
        "--quiet",
        "--config-net",
        "--gateway", PASTA_HOST_IPV4,
        "--address", PASTA_CHILD_IPV4, "--netmask", PASTA_IPV4_NETMASK,
        "--dns-forward", PASTA_HOST_IPV4,
        "--gateway", PASTA_HOST_IPV6,
        "--address", PASTA_CHILD_IPV6,
        "--dns-forward", PASTA_HOST_IPV6,
        "--ns-ifname", PASTA_NS_IFNAME,
        "--netns", "/proc/self/fd/0",
        // clang-format on
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

    runProgram2(
        RunOptions{
            .program = pastaPath,
            .args = args,
            .uid = buildUserId,
            .gid = buildGroupId,
            // TODO these redirections are crimes. pasta closes all non-stdio file
            // descriptors very early and lacks fd arguments for the namespaces we
            // want it to join. we cannot have pasta join the namespaces via pids;
            // doing so requires capabilities which pasta *also* drops very early.
            .redirections =
                {
                    {.from = STDIN_FILENO, .to = netns.get()},
                    {.from = STDOUT_FILENO, .to = userns ? userns.get() : STDOUT_FILENO},
                },
            .caps = getuid() == 0 ? std::set<long>{CAP_SYS_ADMIN, CAP_NET_BIND_SERVICE} : std::set<long>{},
        });
}

std::string rewriteResolvConf(const std::string & fromHost)
{
    static constexpr auto flags = std::regex::ECMAScript;
    static std::regex lineRegex("nameserver\\s.*\\n", flags);

    auto rewritten = std::regex_replace(fromHost, lineRegex, "");
    if (!rewritten.empty() && rewritten.back() != '\n')
        rewritten += '\n';

    rewritten += nix::fmt("nameserver %s\nnameserver %s\n", PASTA_HOST_IPV4, PASTA_HOST_IPV6);

    return rewritten;
}

} // namespace pasta
} // namespace nix
