#include "command.hh"
#include "shared.hh"
#include "local-store.hh"
#include "remote-store.hh"
#include "util.hh"
#include "serialise.hh"
#include "archive.hh"
#include "globals.hh"
#include "derivations.hh"
#include "finally.hh"
#include "legacy.hh"
#include "daemon.hh"

#include <algorithm>
#include <climits>
#include <cstring>

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>

#if __APPLE__ || __FreeBSD__
#include <sys/ucred.h>
#endif

using namespace nix;
using namespace nix::daemon;

#ifndef __linux__
#define SPLICE_F_MOVE 0
static ssize_t splice(int fd_in, void *off_in, int fd_out, void *off_out, size_t len, unsigned int flags)
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
    while (waitpid(-1, 0, WNOHANG) > 0) ;
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


bool matchUser(const string & user, const string & group, const Strings & users)
{
    if (find(users.begin(), users.end(), "*") != users.end())
        return true;

    if (find(users.begin(), users.end(), user) != users.end())
        return true;

    for (auto & i : users)
        if (string(i, 0, 1) == "@") {
            if (group == string(i, 1)) return true;
            struct group * gr = getgrnam(i.c_str() + 1);
            if (!gr) continue;
            for (char * * mem = gr->gr_mem; *mem; mem++)
                if (user == string(*mem)) return true;
        }

    return false;
}


struct PeerInfo
{
    std::optional<pid_t> pid;
    std::optional<uid_t> uid;
    std::optional<gid_t> gid;
};


//  Get the identity of the caller, if possible.
static PeerInfo getPeerInfo(int remote)
{
    PeerInfo peer;

#if defined(SO_PEERCRED)

    ucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == -1)
        throw SysError("getting peer credentials");
    peer.pid = cred.pid;
    peer.uid = cred.uid;
    peer.gid = cred.gid;

#elif defined(LOCAL_PEERCRED)

#if !defined(SOL_LOCAL)
#define SOL_LOCAL 0
#endif

    xucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_LOCAL, LOCAL_PEERCRED, &cred, &credLen) == -1)
        throw SysError("getting peer credentials");
    peer.uid = cred.uid;

#endif

    return peer;
}


#define SD_LISTEN_FDS_START 3


static ref<Store> openUncachedStore()
{
    Store::Params params; // FIXME: get params from somewhere
    // Disable caching since the client already does that.
    params["path-info-cache-size"] = "0";
    return openStore(settings.storeUri, params);
}


static void daemonLoop()
{
    if (chdir("/") == -1)
        throw SysError("cannot change current directory");

    //  Get rid of children automatically; don't let them become zombies.
    setSigChldAction(true);

    AutoCloseFD fdSocket;

    //  Handle socket-based activation by systemd.
    auto listenFds = getEnv("LISTEN_FDS");
    if (listenFds) {
        if (getEnv("LISTEN_PID") != std::to_string(getpid()) || listenFds != "1")
            throw Error("unexpected systemd environment variables");
        fdSocket = SD_LISTEN_FDS_START;
        closeOnExec(fdSocket.get());
    }

    //  Otherwise, create and bind to a Unix domain socket.
    else {
        createDirs(dirOf(settings.nixDaemonSocketFile));
        fdSocket = createUnixDomainSocket(settings.nixDaemonSocketFile, 0666);
    }

    //  Loop accepting connections.
    while (1) {

        try {
            //  Accept a connection.
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote = accept(fdSocket.get(),
                (struct sockaddr *) &remoteAddr, &remoteAddrLen);
            checkInterrupt();
            if (!remote) {
                if (errno == EINTR) continue;
                throw SysError("accepting connection");
            }

            closeOnExec(remote.get());

            TrustedFlag trusted = NotTrusted;
            PeerInfo peer = getPeerInfo(remote.get());

            std::string user, group;

            if (peer.uid) {
                auto pw = getpwuid(*peer.uid);
                user = pw ? pw->pw_name : std::to_string(*peer.uid);
            }

            if (peer.gid) {
                auto gr = getgrgid(*peer.gid);
                auto group = gr ? gr->gr_name : std::to_string(*peer.gid);
            }

            Strings trustedUsers = settings.trustedUsers;
            Strings allowedUsers = settings.allowedUsers;

            if (matchUser(user, group, trustedUsers))
                trusted = Trusted;

            if ((!trusted && !matchUser(user, group, allowedUsers)) || group == settings.buildUsersGroup)
                throw Error("user '%1%' is not allowed to connect to the Nix daemon", user);

            printInfo(
                "accepted connection from pid %s, user %s%s",
                peer.pid ? std::to_string(*peer.pid) : "<unknown>",
                peer.uid ? user : "<unknown>",
                trusted ? " (trusted)" : "");

            //  Fork a child to handle the connection.
            ProcessOptions options;
            options.errorPrefix = "unexpected Nix daemon error: ";
            options.dieWithParent = false;
            options.runExitHandlers = true;
            options.allowVfork = false;
            startProcess([&]() {
                fdSocket = -1;

                //  Background the daemon.
                if (setsid() == -1)
                    throw SysError("creating a new session");

                //  Restore normal handling of SIGCHLD.
                setSigChldAction(false);

                //  For debugging, stuff the pid into argv[1].
                if (peer.pid && savedArgv[1]) {
                    string processName = std::to_string(*peer.pid);
                    strncpy(savedArgv[1], processName.c_str(), strlen(savedArgv[1]));
                }

                //  Handle the connection.
                FdSource from(remote.get());
                FdSink to(remote.get());
                processConnection(openUncachedStore(), from, to, trusted, NotRecursive, [&](Store & store) {
                    if (peer.uid)
                        store.createUser(user, *peer.uid);
                });

                exit(0);
            }, options);

        } catch (Interrupted & e) {
            return;
        } catch (Error & error) {
            ErrorInfo ei = error.info();
            // FIXME: add to trace?
            ei.msg = hintfmt("error processing connection: %1%", ei.msg.str());
            logError(ei);
        }
    }
}

static void runDaemon(bool stdio)
{
    if (stdio) {
        if (auto store = openUncachedStore().dynamic_pointer_cast<RemoteStore>()) {
            auto conn = store->openConnectionWrapper();
            int from = conn->from.fd;
            int to = conn->to.fd;

            auto nfds = std::max(from, STDIN_FILENO) + 1;
            while (true) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(from, &fds);
                FD_SET(STDIN_FILENO, &fds);
                if (select(nfds, &fds, nullptr, nullptr, nullptr) == -1)
                    throw SysError("waiting for data from client or server");
                if (FD_ISSET(from, &fds)) {
                    auto res = splice(from, nullptr, STDOUT_FILENO, nullptr, SSIZE_MAX, SPLICE_F_MOVE);
                    if (res == -1)
                        throw SysError("splicing data from daemon socket to stdout");
                    else if (res == 0)
                        throw EndOfFile("unexpected EOF from daemon socket");
                }
                if (FD_ISSET(STDIN_FILENO, &fds)) {
                    auto res = splice(STDIN_FILENO, nullptr, to, nullptr, SSIZE_MAX, SPLICE_F_MOVE);
                    if (res == -1)
                        throw SysError("splicing data from stdin to daemon socket");
                    else if (res == 0)
                        return;
                }
            }
        } else {
            FdSource from(STDIN_FILENO);
            FdSink to(STDOUT_FILENO);
            /* Auth hook is empty because in this mode we blindly trust the
               standard streams. Limiting access to those is explicitly
               not `nix-daemon`'s responsibility. */
            processConnection(openUncachedStore(), from, to, Trusted, NotRecursive, [&](Store & _){});
        }
    } else
        daemonLoop();
}

static int main_nix_daemon(int argc, char * * argv)
{
    {
        auto stdio = false;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--daemon")
                ; //  ignored for backwards compatibility
            else if (*arg == "--help")
                showManPage("nix-daemon");
            else if (*arg == "--version")
                printVersion("nix-daemon");
            else if (*arg == "--stdio")
                stdio = true;
            else return false;
            return true;
        });

        runDaemon(stdio);

        return 0;
    }
}

static RegisterLegacyCommand r_nix_daemon("nix-daemon", main_nix_daemon);

struct CmdDaemon : StoreCommand
{
    bool stdio = false;

    CmdDaemon()
    {
        addFlag({
            .longName = "stdio",
            .description = "Handle a single connection on stdin/stdout.",
            .handler = {&stdio, true},
        });
    }

    std::string description() override
    {
        return "daemon to perform store operations on behalf of non-root clients";
    }

    Category category() override { return catUtility; }

    std::string doc() override
    {
        return
          #include "daemon.md"
          ;
    }

    void run(ref<Store> store) override
    {
        runDaemon(stdio);
    }
};

static auto rCmdDaemon = registerCommand2<CmdDaemon>({"daemon"});
