#include "shared.hh"
#include "util.hh"
#include "serialise.hh"
#include "archive.hh"
#include "affinity.hh"
#include "globals.hh"

#include <iostream>
#include <unistd.h>

using namespace nix;
using std::pair;
using std::cout;
using std::endl;

// !!! TODO:
// * Respect more than the first host
// * use a database
// * show progress

static pair<FdSink, FdSource> connect(string conn) {
    Pipe to, from;
    to.create();
    from.create();
    pid_t child = fork();
    switch (child) {
        case -1:
            throw SysError("unable to fork");
        case 0:
            try {
                restoreAffinity();
                if (dup2(to.readSide, STDIN_FILENO) == -1)
                    throw SysError("dupping stdin");
                if (dup2(from.writeSide, STDOUT_FILENO) == -1)
                    throw SysError("dupping stdout");
                execlp("ssh"
                      , "ssh"
                      , "-x"
                      , "-T"
                      , conn.c_str()
                      , "nix-store --serve"
                      , NULL);
                throw SysError("executing ssh");
            } catch (std::exception & e) {
                std::cerr << "error: " << e.what() << std::endl;
            }
            _exit(1);
    }
    // If child exits unexpectedly, we'll EPIPE. If we exit unexpectedly, child will
    // So no need to keep track of it.

    return pair<FdSink, FdSource>(to.writeSide.borrow(), from.readSide.borrow());
}

static void substitute(pair<FdSink, FdSource> & pipes, Path storePath, Path destPath) {
    writeString("substitute", pipes.first);
    writeString(storePath, pipes.first);
    pipes.first.flush();
    restorePath(destPath, pipes.second);
    cout << endl;
}

static void query(pair<FdSink, FdSource> & pipes) {
    using std::cin;
    writeString("query", pipes.first);
    for (string line; getline(cin, line);) {
        Strings tokenized = tokenizeString<Strings>(line);
        string cmd = tokenized.front();
        writeString(cmd, pipes.first);
        tokenized.pop_front();
        foreach (Strings::iterator, i, tokenized)
        writeStrings(tokenized, pipes.first);
        pipes.first.flush();
        if (cmd == "have") {
            PathSet paths = readStrings<PathSet>(pipes.second);
            foreach (PathSet::iterator, i, paths)
                cout << *i << endl;
        } else if (cmd == "info") {
            for (Path path = readString(pipes.second); !path.empty(); path = readString(pipes.second)) {
                cout << path << endl;
                cout << readString(pipes.second) << endl;
                PathSet references = readStrings<PathSet>(pipes.second);
                cout << references.size() << endl;
                foreach (PathSet::iterator, i, references)
                    cout << *i << endl;
                cout << readLongLong(pipes.second) << endl;
                cout << readLongLong(pipes.second) << endl;
            }
        } else
            throw Error(format("Unknown substituter query `%1%'") % cmd);
        cout << endl;
    }
    writeString("", pipes.first);
}

void run(Strings args)
{
    if (args.empty())
        throw UsageError("download-via-ssh requires an argument");

    if (settings.sshSubstituterHosts.empty())
        return;

    cout << endl;

    pair<FdSink, FdSource> pipes = connect(settings.sshSubstituterHosts.front());

    Strings::iterator i = args.begin();
    if (*i == "--query")
        query(pipes);
    else if (*i == "--substitute")
        if (args.size() != 3)
            throw UsageError("download-via-ssh: --substitute takes exactly two arguments");
        else {
            Path storePath = *++i;
            Path destPath = *++i;
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
