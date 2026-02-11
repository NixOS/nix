///@file

#include "nix/util/signals.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/local-store.hh"
#include "nix/store/remote-store.hh"
#include "nix/store/remote-store-connection.hh"
#include "nix/store/store-open.hh"
#include "nix/util/serialise.hh"
#include "nix/util/archive.hh"
#include "nix/store/globals.hh"
#include "nix/util/config-global.hh"
#include "nix/store/derivations.hh"
#include "nix/util/finally.hh"
#include "nix/cmd/legacy.hh"
#include "nix/cmd/unix-socket-server.hh"
#include "nix/store/daemon.hh"
#include "man-pages.hh"

#include <algorithm>
#include <climits>
#include <cstring>

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>

#ifdef __linux__
#  include "nix/util/cgroup.hh"
#endif

using namespace nix;
using namespace nix::daemon;

/**
 * Settings related to authenticating clients for the Nix daemon.
 *
 * For pipes we have little good information about the client side, but
 * for Unix domain sockets we do. So currently these options implemented
 * mandatory access control based on user names and group names (looked
 * up and translated to UID/GIDs in the CLI process that runs the code
 * in this file).
 *
 * No code outside of this file knows about these settings (this is not
 * exposed in a header); all authentication and authorization happens in
 * `daemon.cc`.
 */
struct AuthorizationSettings : Config
{

    Setting<Strings> trustedUsers{
        this,
        {"root"},
        "trusted-users",
        R"(
          A list of user names, separated by whitespace.
          These users will have additional rights when connecting to the Nix daemon, such as the ability to specify additional [substituters](#conf-substituters), or to import unsigned realisations or unsigned input-addressed store objects.

          You can also specify groups by prefixing names with `@`.
          For instance, `@wheel` means all users in the `wheel` group.

          > **Warning**
          >
          > Adding a user to `trusted-users` is essentially equivalent to giving that user root access to the system.
          > For example, the user can access or replace store path contents that are critical for system security.
        )"};

    /**
     * Who we trust to use the daemon in safe ways
     */
    Setting<Strings> allowedUsers{
        this,
        {"*"},
        "allowed-users",
        R"(
          A list of user names, separated by whitespace.
          These users are allowed to connect to the Nix daemon.

          You can specify groups by prefixing names with `@`.
          For instance, `@wheel` means all users in the `wheel` group.
          Also, you can allow all users by specifying `*`.

          > **Note**
          >
          > Trusted users (set in [`trusted-users`](#conf-trusted-users)) can always connect to the Nix daemon.
        )"};
};

AuthorizationSettings authorizationSettings;

static GlobalConfig::Register rSettings(&authorizationSettings);

#ifndef __linux__
#  define SPLICE_F_MOVE 0

static ssize_t splice(int fd_in, void * off_in, int fd_out, void * off_out, size_t len, unsigned int flags)
{
    // We ignore most parameters, we just have them for conformance with the linux syscall
    std::vector<char> buf(8192);
    auto read_count = read(fd_in, buf.data(), buf.size());
    if (read_count == -1)
        return read_count;
    auto write_count = decltype(read_count)(0);
    while (write_count < read_count) {
        auto res = write(fd_out, buf.data() + write_count, read_count - write_count);
        if (res == -1)
            return res;
        write_count += res;
    }
    return read_count;
}
#endif

static void sigChldHandler(int sigNo)
{
    // Ensure we don't modify errno of whatever we've interrupted
    auto saved_errno = errno;
    //  Reap all dead children.
    while (waitpid(-1, 0, WNOHANG) > 0)
        ;
    errno = saved_errno;
}

static void setSigChldAction(bool autoReap)
{
    struct sigaction act, oact;
    act.sa_handler = autoReap ? sigChldHandler : SIG_DFL;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGCHLD, &act, &oact))
        throw SysError("setting SIGCHLD handler");
}

/**
 * @return Is the given user a member of this group?
 *
 * @param user User specified by username.
 *
 * @param group Group the user might be a member of.
 */
static bool matchUser(std::string_view user, const struct group & gr)
{
    for (char ** mem = gr.gr_mem; *mem; mem++)
        if (user == std::string_view(*mem))
            return true;
    return false;
}

/**
 * Does the given user (specified by user name and primary group name)
 * match the given user/group whitelist?
 *
 * If the list allows all users: Yes.
 *
 * If the username is in the set: Yes.
 *
 * If the groupname is in the set: Yes.
 *
 * If the user is in another group which is in the set: yes.
 *
 * Otherwise: No.
 */
static bool
matchUser(const std::optional<std::string> & user, const std::optional<std::string> & group, const Strings & users)
{
    if (find(users.begin(), users.end(), "*") != users.end())
        return true;

    if (user && find(users.begin(), users.end(), *user) != users.end())
        return true;

    for (auto & i : users)
        if (i.substr(0, 1) == "@") {
            if (group && *group == i.substr(1))
                return true;
            struct group * gr = getgrnam(i.c_str() + 1);
            if (!gr)
                continue;
            if (user && matchUser(*user, *gr))
                return true;
        }

    return false;
}

/**
 * Authenticate a potential client
 *
 * @param peer Information about other end of the connection, the client which
 * wants to communicate with us.
 *
 * @return A pair of a `TrustedFlag`, whether the potential client is trusted,
 * and the name of the user (useful for printing messages).
 *
 * If the potential client is not allowed to talk to us, we throw an `Error`.
 */
static std::pair<TrustedFlag, std::optional<std::string>> authPeer(const unix::PeerInfo & peer)
{
    TrustedFlag trusted = NotTrusted;

    auto pw = peer.uid ? getpwuid(*peer.uid) : nullptr;
    auto user = pw         ? std::optional<std::string>(pw->pw_name)
                : peer.uid ? std::optional(std::to_string(*peer.uid))
                           : std::nullopt;

    auto gr = peer.gid ? getgrgid(*peer.gid) : 0;
    auto group = gr         ? std::optional<std::string>(gr->gr_name)
                 : peer.gid ? std::optional(std::to_string(*peer.gid))
                            : std::nullopt;

    const Strings & trustedUsers = authorizationSettings.trustedUsers;
    const Strings & allowedUsers = authorizationSettings.allowedUsers;

    if (matchUser(user, group, trustedUsers))
        trusted = Trusted;

    if ((!trusted && !matchUser(user, group, allowedUsers)) || group == settings.getLocalSettings().buildUsersGroup)
        throw Error("user '%1%' is not allowed to connect to the Nix daemon", user.value_or("<unknown>"));

    return {trusted, std::move(user)};
}

/**
 * Run a server. The loop opens a socket and accepts new connections from that
 * socket.
 *
 * @param storeConfig The store configuration to use for opening stores.
 * @param forceTrustClientOpt If present, force trusting or not trusted
 * the client. Otherwise, decide based on the authentication settings
 * and user credentials (from the unix domain socket).
 */
static void daemonLoop(ref<const StoreConfig> storeConfig, std::optional<TrustedFlag> forceTrustClientOpt)
{
    if (chdir("/") == -1)
        throw SysError("cannot change current directory");

    //  Get rid of children automatically; don't let them become zombies.
    setSigChldAction(true);

#ifdef __linux__
    if (settings.getLocalSettings().useCgroups) {
        experimentalFeatureSettings.require(Xp::Cgroups);

        //  This also sets the root cgroup to the current one.
        auto rootCgroup = getRootCgroup();
        auto cgroupFS = getCgroupFS();
        if (!cgroupFS)
            throw Error("cannot determine the cgroups file system");
        auto rootCgroupPath = *cgroupFS / rootCgroup.rel();
        if (!pathExists(rootCgroupPath))
            throw Error("expected cgroup directory %s", PathFmt(rootCgroupPath));
        auto daemonCgroupPath = rootCgroupPath + "/nix-daemon";
        //  Create new sub-cgroup for the daemon.
        if (mkdir(daemonCgroupPath.c_str(), 0755) != 0 && errno != EEXIST)
            throw SysError("creating cgroup '%s'", daemonCgroupPath);
        //  Move daemon into the new cgroup.
        writeFile(daemonCgroupPath + "/cgroup.procs", fmt("%d", getpid()));
    }
#endif

    auto socketPath = [&]() -> std::filesystem::path {
        if (auto * localFSConfig = dynamic_cast<const LocalFSStoreConfig *>(&*storeConfig))
            return localFSConfig->getDefaultDaemonSocketPath();
        throw Error("cannot run daemon for non-local-filesystem store");
    }();

    try {
        unix::serveUnixSocket(
            {
                .socketPath = socketPath,
                .socketMode = 0666,
            },
            [&](AutoCloseFD remote, std::function<void()> closeListeners) {
                unix::closeOnExec(remote.get());

                unix::PeerInfo peer;
                TrustedFlag trusted;
                std::optional<std::string> userName;

                if (forceTrustClientOpt)
                    trusted = *forceTrustClientOpt;
                else {
                    peer = unix::getPeerInfo(remote.get());
                    auto [_trusted, _userName] = authPeer(peer);
                    trusted = _trusted;
                    userName = _userName;
                };

                printInfo(
                    (std::string) "accepted connection from pid %1%, user %2%" + (trusted ? " (trusted)" : ""),
                    peer.pid ? std::to_string(*peer.pid) : "<unknown>",
                    userName.value_or("<unknown>"));

                // Fork a child to handle the connection.
                ProcessOptions options;
                options.errorPrefix = "unexpected Nix daemon error: ";
                options.dieWithParent = false;
                options.runExitHandlers = true;
                options.allowVfork = false;
                startProcess(
                    [&, storeConfig, closeListeners = std::move(closeListeners)]() {
                        closeListeners();

                        // Background the daemon.
                        if (setsid() == -1)
                            throw SysError("creating a new session");

                        // Restore normal handling of SIGCHLD.
                        setSigChldAction(false);

                        // For debugging, stuff the pid into argv[1].
                        if (peer.pid && savedArgv[1]) {
                            auto processName = std::to_string(*peer.pid);
                            strncpy(savedArgv[1], processName.c_str(), strlen(savedArgv[1]));
                        }

                        // Handle the connection.
                        auto store = storeConfig->openStore();
                        store->init();
                        processConnection(store, FdSource(remote.get()), FdSink(remote.get()), trusted, NotRecursive);

                        exit(0);
                    },
                    options);
            });
    } catch (Interrupted & e) {
        return;
    }
}

/**
 * Forward a standard IO connection to the given remote store.
 *
 * We just act as a middleman blindly ferry output between the standard
 * input/output and the remote store connection, not processing anything.
 *
 * Loops until standard input disconnects, or an error is encountered.
 */
static void forwardStdioConnection(RemoteStore & store)
{
    auto conn = store.openConnectionWrapper();
    int from = conn->from.fd;
    int to = conn->to.fd;

    Socket fromSock = toSocket(from), stdinSock = toSocket(getStandardInput());
    auto nfds = std::max(fromSock, stdinSock) + 1;
    while (true) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fromSock, &fds);
        FD_SET(stdinSock, &fds);
        if (select(nfds, &fds, nullptr, nullptr, nullptr) == -1)
            throw SysError("waiting for data from client or server");
        if (FD_ISSET(fromSock, &fds)) {
            auto res = splice(from, nullptr, STDOUT_FILENO, nullptr, SSIZE_MAX, SPLICE_F_MOVE);
            if (res == -1)
                throw SysError("splicing data from daemon socket to stdout");
            else if (res == 0)
                throw EndOfFile("unexpected EOF from daemon socket");
        }
        if (FD_ISSET(stdinSock, &fds)) {
            auto res = splice(STDIN_FILENO, nullptr, to, nullptr, SSIZE_MAX, SPLICE_F_MOVE);
            if (res == -1)
                throw SysError("splicing data from stdin to daemon socket");
            else if (res == 0)
                return;
        }
    }
}

/**
 * Process a client connecting to us via standard input/output
 *
 * Unlike `forwardStdioConnection()` we do process commands ourselves in
 * this case, not delegating to another daemon.
 *
 * @param trustClient Whether to trust the client. Forwarded directly to
 * `processConnection()`.
 */
static void processStdioConnection(ref<Store> store, TrustedFlag trustClient)
{
    processConnection(store, FdSource(STDIN_FILENO), FdSink(STDOUT_FILENO), trustClient, NotRecursive);
}

/**
 * Entry point shared between the new CLI `nix daemon` and old CLI
 * `nix-daemon`.
 *
 * @param storeConfig The store configuration to use for opening stores.
 * @param forceTrustClientOpt See `daemonLoop()` and the parameter with
 * the same name over there for details.
 *
 * @param processOps Whether to force processing ops even if the next
 * store also is a remote store and could process it directly.
 */
static void
runDaemon(ref<StoreConfig> storeConfig, bool stdio, std::optional<TrustedFlag> forceTrustClientOpt, bool processOps)
{
    // Disable caching since the client already does that.
    storeConfig->pathInfoCacheSize = 0;

    if (stdio) {
        auto store = storeConfig->openStore();
        store->init();

        std::shared_ptr<RemoteStore> remoteStore;

        // If --force-untrusted is passed, we cannot forward the connection and
        // must process it ourselves (before delegating to the next store) to
        // force untrusting the client.
        processOps |= !forceTrustClientOpt || *forceTrustClientOpt != NotTrusted;

        if (!processOps && (remoteStore = store.dynamic_pointer_cast<RemoteStore>()))
            forwardStdioConnection(*remoteStore);
        else
            // `Trusted` is passed in the auto (no override case) because we
            // cannot see who is on the other side of a plain pipe. Limiting
            // access to those is explicitly not `nix-daemon`'s responsibility.
            processStdioConnection(store, forceTrustClientOpt.value_or(Trusted));
    } else
        daemonLoop(storeConfig, forceTrustClientOpt);
}

static int main_nix_daemon(int argc, char ** argv)
{
    {
        auto stdio = false;
        std::optional<TrustedFlag> isTrustedOpt = std::nullopt;
        auto processOps = false;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--daemon")
                ; //  ignored for backwards compatibility
            else if (*arg == "--help")
                showManPage("nix-daemon");
            else if (*arg == "--version")
                printVersion("nix-daemon");
            else if (*arg == "--stdio")
                stdio = true;
            else if (*arg == "--force-trusted") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = Trusted;
            } else if (*arg == "--force-untrusted") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = NotTrusted;
            } else if (*arg == "--default-trust") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = std::nullopt;
            } else if (*arg == "--process-ops") {
                experimentalFeatureSettings.require(Xp::MountedSSHStore);
                processOps = true;
            } else
                return false;
            return true;
        });

        runDaemon(resolveStoreConfig(StoreReference{settings.storeUri.get()}), stdio, isTrustedOpt, processOps);

        return 0;
    }
}

static RegisterLegacyCommand r_nix_daemon("nix-daemon", main_nix_daemon);

struct CmdDaemon : StoreConfigCommand
{
    bool stdio = false;
    std::optional<TrustedFlag> isTrustedOpt = std::nullopt;
    bool processOps = false;

    CmdDaemon()
    {
        addFlag({
            .longName = "stdio",
            .description = "Attach to standard I/O, instead of using UNIX socket(s).",
            .handler = {&stdio, true},
        });

        addFlag({
            .longName = "force-trusted",
            .description = "Force the daemon to trust connecting clients.",
            .handler = {[&]() { isTrustedOpt = Trusted; }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });

        addFlag({
            .longName = "force-untrusted",
            .description =
                "Force the daemon to not trust connecting clients. The connection is processed by the receiving daemon before forwarding commands.",
            .handler = {[&]() { isTrustedOpt = NotTrusted; }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });

        addFlag({
            .longName = "default-trust",
            .description = "Use Nix's default trust.",
            .handler = {[&]() { isTrustedOpt = std::nullopt; }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });

        addFlag({
            .longName = "process-ops",
            .description = R"(
              Forces the daemon to process received commands itself rather than forwarding the commands straight to the remote store.

              This is useful for the `mounted-ssh://` store where some actions need to be performed on the remote end but as connected user, and not as the user of the underlying daemon on the remote end.
            )",
            .handler = {[&]() { processOps = true; }},
            .experimentalFeature = Xp::MountedSSHStore,
        });
    }

    std::string description() override
    {
        return "daemon to perform store operations on behalf of non-root clients";
    }

    Category category() override
    {
        return catUtility;
    }

    std::string doc() override
    {
        return
#include "daemon.md"
            ;
    }

    void run(ref<StoreConfig> storeConfig) override
    {
        runDaemon(std::move(storeConfig), stdio, isTrustedOpt, processOps);
    }
};

static auto rCmdDaemon = registerCommand2<CmdDaemon>({"daemon"});
