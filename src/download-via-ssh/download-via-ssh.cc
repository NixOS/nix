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
    writeInt(cmdDumpStorePath, pipes.first);
    writeString(storePath, pipes.first);
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
            writeInt(cmdQueryValidPaths, pipes.first);
            writeInt(0, pipes.first); // don't lock
            writeInt(0, pipes.first); // don't substitute
            writeStrings(tokenized, pipes.first);
            pipes.first.flush();
            PathSet paths = readStrings<PathSet>(pipes.second);
            foreach (PathSet::iterator, i, paths)
                std::cout << *i << std::endl;
        } else if (cmd == "info") {
            writeInt(cmdQueryPathInfos, pipes.first);
            writeStrings(tokenized, pipes.first);
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
                foreach (PathSet::iterator, i, references)
                    std::cout << *i << std::endl;
                std::cout << readLongLong(pipes.second) << std::endl;
                std::cout << readLongLong(pipes.second) << std::endl;
            }
        } else
            throw Error(format("unknown substituter query `%1%'") % cmd);
        std::cout << std::endl;
    }
}


void run(Strings args)
{
    if (args.empty())
        throw UsageError("download-via-ssh requires an argument");

    if (settings.sshSubstituterHosts.empty())
        return;

    std::cout << std::endl;

    /* Pass on the location of the daemon client's SSH authentication
       socket. */
    string sshAuthSock = settings.get("ssh-auth-sock", "");
    if (sshAuthSock != "") setenv("SSH_AUTH_SOCK", sshAuthSock.c_str(), 1);

    string host = settings.sshSubstituterHosts.front();
    std::pair<FdSink, FdSource> pipes = connect(host);

    /* Exchange the greeting */
    writeInt(SERVE_MAGIC_1, pipes.first);
    pipes.first.flush();
    unsigned int magic = readInt(pipes.second);
    if (magic != SERVE_MAGIC_2)
        throw Error("protocol mismatch");
    readInt(pipes.second); // Server version, unused for now
    writeInt(SERVE_PROTOCOL_VERSION, pipes.first);
    pipes.first.flush();

    Strings::iterator i = args.begin();
    if (*i == "--query")
        query(pipes);
    else if (*i == "--substitute")
        if (args.size() != 3)
            throw UsageError("download-via-ssh: --substitute takes exactly two arguments");
        else {
            Path storePath = *++i;
            Path destPath = *++i;
            printMsg(lvlError, format("downloading `%1%' via SSH from `%2%'...") % storePath % host);
            substitute(pipes, storePath, destPath);
        }
    else
        throw UsageError(format("download-via-ssh: unknown command `%1%'") % *i);
}


void printHelp()
{
    std::cerr << "Usage: download-via-ssh --query|--substitute store-path dest-path" << std::endl;
}


string programId = "download-via-ssh";
