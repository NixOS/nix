#include "find-roots.hh"
#include <getopt.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <signal.h>

using namespace nix::roots_tracer;

void logStderr(std::string_view msg)
{
  std::cerr << msg << std::endl;
}

TracerConfig parseCmdLine(int argc, char** argv)
{
    std::function<void(std::string_view msg)> log = logStderr;
    std::function<void(std::string_view msg)> debug = logNone;
    fs::path storeDir = "/nix/store";
    fs::path stateDir = "/nix/var/nix";
    fs::path socketPath = "/nix/var/nix/gc-trace-socket/socket";

    auto usage = [&]() {
        std::string programName = argc > 0 ? argv[0] : "nix-find-roots";
        std::cerr << "Usage: " << programName << " [--verbose|-v] [-s storeDir] [-d stateDir] [-l socketPath]" << std::endl;
        exit(1);
    };
    static struct option long_options[] = {
        { "verbose", no_argument, 0, 'v' },
        { "socket_path", required_argument, 0, 'l' },
        { "store_dir", required_argument, 0, 's' },
        { "state_dir", required_argument, 0, 'd' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 },
    };

    int option_index = 0;
    int opt_char;
    while((opt_char = getopt_long(argc, argv, "vd:s:l:h",
                    long_options, &option_index)) != -1) {
        switch (opt_char) {
            case 0:
                break;
          break;
            case '?':
            case 'h':
                usage();
                break;
            case 'v':
                debug = logStderr;
                break;
            case 's':
                storeDir = fs::path(optarg);
                break;
            case 'd':
                stateDir = fs::path(optarg);
                break;
            case 'l':
                socketPath = fs::path(optarg);
                break;
            default:
                std::cerr << "Got invalid char: " << (char)opt_char << std::endl;
                abort();
        }
    };
    return TracerConfig {
        .storeDir = storeDir,
        .stateDir = stateDir,
        .socketPath = socketPath,
        .debug = debug,
    };
}

/**
 * Return `original` with every newline or tab character escaped
 */
std::string escape(std::string original)
{
    map<string, string> replacements = {
        {"\n", "\\n"},
        {"\t", "\\t"},
    };
    for (auto [oldStr, newStr] : replacements) {
        size_t currentPos = 0;
        while ((currentPos = original.find(oldStr, currentPos)) != std::string::npos) {
            original.replace(currentPos, oldStr.length(), newStr);
            currentPos += newStr.length();
        }
    }

    return original;
}

#define SD_LISTEN_FDS_START 3 // Like in systemd

int main(int argc, char * * argv)
{
    const TracerConfig opts = parseCmdLine(argc, argv);
    const set<fs::path> standardRoots = {
        opts.stateDir / fs::path("profiles"),
        opts.stateDir / fs::path("gcroots"),
    };

    int mySock;

    //  Handle socket-based activation by systemd.
    auto rawListenFds = std::getenv("LISTEN_FDS");
    if (rawListenFds) {
        auto listenFds = std::string(rawListenFds);
        auto listenPid = std::getenv("LISTEN_PID");
        if (listenPid == nullptr || listenPid != std::to_string(getpid()) || listenFds != "1")
            throw Error("unexpected systemd environment variables");
        mySock = SD_LISTEN_FDS_START;
    } else {
        mySock = socket(PF_UNIX, SOCK_STREAM, 0);
        if (mySock == -1) {
            throw Error(std::string("Cannot create Unix domain socket, got") +
                    std::strerror(errno));
        }
        struct sockaddr_un addr;
        addr.sun_family = AF_UNIX;

        auto socketDir = opts.socketPath.parent_path();
        auto socketFilename = opts.socketPath.filename();
        if (socketFilename.string().size() > sizeof(addr.sun_path))
            throw Error(
                    "Socket filename " + socketFilename.string() +
                    " is too long, should be at most " +
                    std::to_string(sizeof(addr.sun_path))
                    );
        chdir(socketDir.c_str());

        fs::remove(socketFilename);
        if (socketFilename.string().size() + 1 >= sizeof(addr.sun_path))
            throw Error("socket path '" + socketFilename.string() + "' is too long");
        strcpy(addr.sun_path, socketFilename.c_str());
        if (bind(mySock, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
            throw Error("Cannot bind to socket");
        }

        if (listen(mySock, 5) == -1)
            throw Error("cannot listen on socket " + opts.socketPath.string());
    }

    // Ignore SIGPIPE so that an interrupted connection doesn’t stop the daemon
    signal(SIGPIPE, SIG_IGN);

    while (1) {
        struct sockaddr_un remoteAddr;
        socklen_t remoteAddrLen = sizeof(remoteAddr);
        int remoteSocket = accept(
                mySock,
                (struct sockaddr*) & remoteAddr,
                &remoteAddrLen
                );

        if (remoteSocket == -1) {
            if (errno == EINTR) continue;
            throw Error("Error accepting the connection");
        }

        opts.log("accepted connection");

        auto printToSocket = [&](std::string_view s) {
            send(remoteSocket, s.data(), s.size(), 0);
        };

        auto traceResult = traceStaticRoots(opts, standardRoots);
        auto runtimeRoots = getRuntimeRoots(opts);
        traceResult.storeRoots.insert(runtimeRoots.begin(), runtimeRoots.end());
        for (auto & [rootInStore, externalRoots] : traceResult.storeRoots) {
            for (auto & externalRoot : externalRoots) {
                printToSocket(escape(rootInStore.string()));
                printToSocket("\t");
                printToSocket(escape(externalRoot.string()));
                printToSocket("\n");
            }

        }
        printToSocket("\n");
        for (auto & deadLink : traceResult.deadLinks) {
            printToSocket(escape(deadLink.string()));
            printToSocket("\n");
        }

        close(remoteSocket);
    }
}
