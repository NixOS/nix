#include "shared.hh"
#include "util.hh"
#include "serialise.hh"
#include "archive.hh"
#include "affinity.hh"
#include "globals.hh"
#include "serve-protocol.hh"
#include "worker-protocol.hh"
#include "store-api.hh"

#include <iostream>
#include <cstdlib>
#include <unistd.h>

using namespace nix;

// !!! TODO:
// * Respect more than the first host
// * use a database
// * show progress


static std::pair<FdSink, FdSource> connect(const string & conn)
{
    Pipe to, from;
    to.create();
    from.create();
    startProcess([&]() {
        if (dup2(to.readSide, STDIN_FILENO) == -1)
            throw SysError("dupping stdin");
        if (dup2(from.writeSide, STDOUT_FILENO) == -1)
            throw SysError("dupping stdout");
        execlp("ssh", "ssh", "-x", "-T", conn.c_str(), "nix-store --serve", NULL);
        throw SysError("executing ssh");
    });
    // If child exits unexpectedly, we'll EPIPE or EOF early.
    // If we exit unexpectedly, child will EPIPE or EOF early.
    // So no need to keep track of it.

    return std::pair<FdSink, FdSource>(to.writeSide.borrow(), from.readSide.borrow());
}


static void substitute(std::pair<FdSink, FdSource> & pipes, Path storePath, Path destPath)
{
    pipes.first << cmdDumpStorePath << storePath;
    pipes.first.flush();
    restorePath(destPath, pipes.second);
    std::cout << std::endl;
}


static void query(std::pair<FdSink, FdSource> & pipes)
{
    for (string line; getline(std::cin, line);) {
        Strings tokenized = tokenizeString<Strings>(line);
        string cmd = tokenized.front();
        tokenized.pop_front();
        if (cmd == "have") {
            pipes.first
                << cmdQueryValidPaths
                << 0 // don't lock
                << 0 // don't substitute
                << tokenized;
            pipes.first.flush();
            PathSet paths = readStrings<PathSet>(pipes.second);
            for (auto & i : paths)
                std::cout << i << std::endl;
        } else if (cmd == "info") {
            pipes.first << cmdQueryPathInfos << tokenized;
            pipes.first.flush();
            while (1) {
                Path path = readString(pipes.second);
                if (path.empty()) break;
                assertStorePath(path);
                std::cout << path << std::endl;
                string deriver = readString(pipes.second);
                if (!deriver.empty()) assertStorePath(deriver);
                std::cout << deriver << std::endl;
                PathSet references = readStorePaths<PathSet>(pipes.second);
                std::cout << references.size() << std::endl;
                for (auto & i : references)
                    std::cout << i << std::endl;
                std::cout << readLongLong(pipes.second) << std::endl;
                std::cout << readLongLong(pipes.second) << std::endl;
            }
        } else
            throw Error(format("unknown substituter query ‘%1%’") % cmd);
        std::cout << std::endl;
    }
}


int main(int argc, char * * argv)
{
    return handleExceptions(argv[0], [&]() {
        if (argc < 2)
            throw UsageError("download-via-ssh requires an argument");

        initNix();

        settings.update();

        if (settings.sshSubstituterHosts.empty())
            return;

        std::cout << std::endl;

        /* Pass on the location of the daemon client's SSH
           authentication socket. */
        string sshAuthSock = settings.get("ssh-auth-sock", string(""));
        if (sshAuthSock != "") setenv("SSH_AUTH_SOCK", sshAuthSock.c_str(), 1);

        string host = settings.sshSubstituterHosts.front();
        std::pair<FdSink, FdSource> pipes = connect(host);

        /* Exchange the greeting */
        pipes.first << SERVE_MAGIC_1;
        pipes.first.flush();
        unsigned int magic = readInt(pipes.second);
        if (magic != SERVE_MAGIC_2)
            throw Error("protocol mismatch");
        readInt(pipes.second); // Server version, unused for now
        pipes.first << SERVE_PROTOCOL_VERSION;
        pipes.first.flush();

        string arg = argv[1];
        if (arg == "--query")
            query(pipes);
        else if (arg == "--substitute") {
            if (argc != 4)
                throw UsageError("download-via-ssh: --substitute takes exactly two arguments");
            Path storePath = argv[2];
            Path destPath = argv[3];
            printMsg(lvlError, format("downloading ‘%1%’ via SSH from ‘%2%’...") % storePath % host);
            substitute(pipes, storePath, destPath);
        }
        else
            throw UsageError(format("download-via-ssh: unknown command ‘%1%’") % arg);
    });
}
