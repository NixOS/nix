#include "find-roots.hh"
#include <getopt.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <iostream>
#include <unistd.h>
#include <cstring>

using namespace nix::roots_tracer;

void logStderr(std::string_view msg)
{
  std::cerr << msg << std::endl;
}

TracerConfig parseCmdLine(int argc, char** argv)
{
    TracerConfig res;
    res.log = logStderr;
    auto usage = [&]() {
        std::cerr << "Usage: " << string(argv[0]) << " [--verbose|-v] [-s storeDir] [-d stateDir] [-l socketPath]" << std::endl;
        exit(1);
    };
    static struct option long_options[] = {
        { "verbose", no_argument, 0, 'v' },
        { "socket_path", required_argument, 0, 'l' },
        { "store_dir", required_argument, 0, 's' },
        { "state_dir", required_argument, 0, 'd' },
    };

    int option_index = 0;
    int opt_char;
    while((opt_char = getopt_long(argc, argv, "vd:s:l:",
                    long_options, &option_index)) != -1) {
        switch (opt_char) {
            case 0:
                break;
          break;
            case '?':
                usage();
                break;
            case 'v':
                res.debug = logStderr;
                break;
            case 's':
                res.storeDir = fs::path(optarg);
                break;
            case 'd':
                res.stateDir = fs::path(optarg);
                break;
            case 'l':
                res.socketPath = fs::path(optarg);
                break;
            default:
                std::cerr << "Got invalid char: " << (char)opt_char << std::endl;
                abort();
        }
    };
    return res;
}


int main(int argc, char * * argv)
{
    const TracerConfig opts = parseCmdLine(argc, argv);
    const set<fs::path> standardRoots = {
        opts.stateDir / fs::path("profiles"),
        opts.stateDir / fs::path("gcroots"),
    };

    int mySock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (mySock == 0) {
        throw Error("Cannot create Unix domain socket");
    }
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    unlink(opts.socketPath.c_str());
    strcpy(addr.sun_path, opts.socketPath.c_str());
    if (bind(mySock, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
        throw Error("Cannot bind to socket");
    }

    if (listen(mySock, 5) == -1)
        throw Error("cannot listen on socket " + opts.socketPath.string());

    addr.sun_family = AF_UNIX;
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

        auto traceResult = traceStaticRoots(opts, standardRoots);
        auto runtimeRoots = getRuntimeRoots(opts);
        traceResult.storeRoots.insert(runtimeRoots.begin(), runtimeRoots.end());
        for (auto & [rootInStore, externalRoots] : traceResult.storeRoots) {
            for (auto & externalRoot : externalRoots) {
                send(remoteSocket, rootInStore.string().c_str(), rootInStore.string().size(), 0);
                send(remoteSocket, "\t", strlen("\t"), 0);
                send(remoteSocket, externalRoot.string().c_str(), externalRoot.string().size(), 0);
                send(remoteSocket, "\n", strlen("\n"), 0);
            }

        }
        send(remoteSocket, "\n", strlen("\n"), 0);
        for (auto & deadLink : traceResult.deadLinks) {
            send(remoteSocket, deadLink.string().c_str(), deadLink.string().size(), 0);
            send(remoteSocket, "\n", strlen("\n"), 0);
        }

        close(remoteSocket);
    }
}
