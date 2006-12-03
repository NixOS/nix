#include "shared.hh"
#include "local-store.hh"
#include "util.hh"
#include "serialise.hh"
#include "worker-protocol.hh"
#include "archive.hh"

#include <iostream>

using namespace nix;


Path readStorePath(Source & from)
{
    Path path = readString(from);
    assertStorePath(path);
    return path;
}


PathSet readStorePaths(Source & from)
{
    PathSet paths = readStringSet(from);
    for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i)
        assertStorePath(*i);
    return paths;
}


void processConnection(Source & from, Sink & to)
{
    store = boost::shared_ptr<StoreAPI>(new LocalStore(true));

    unsigned int magic = readInt(from);
    if (magic != WORKER_MAGIC_1) throw Error("protocol mismatch");

    writeInt(WORKER_MAGIC_2, to);

    debug("greeting exchanged");

    bool quit = false;

    unsigned int opCount = 0;
    
    do {
        
        WorkerOp op = (WorkerOp) readInt(from);

        opCount++;

        switch (op) {

        case wopQuit: {
            /* Close the database. */
            store.reset((StoreAPI *) 0);
            writeInt(1, to);
            quit = true;
            break;
        }

        case wopIsValidPath: {
            Path path = readStorePath(from);
            writeInt(store->isValidPath(path), to);
            break;
        }

        case wopHasSubstitutes: {
            Path path = readStorePath(from);
            writeInt(store->hasSubstitutes(path), to);
            break;
        }

        case wopQueryPathHash: {
            Path path = readStorePath(from);
            writeString(printHash(store->queryPathHash(path)), to);
            break;
        }

        case wopQueryReferences:
        case wopQueryReferrers: {
            Path path = readStorePath(from);
            PathSet paths;
            if (op == wopQueryReferences)
                store->queryReferences(path, paths);
            else
                store->queryReferrers(path, paths);
            writeStringSet(paths, to);
            break;
        }

        case wopAddToStore: {
            /* !!! uberquick hack */
            string baseName = readString(from);
            bool fixed = readInt(from) == 1;
            bool recursive = readInt(from) == 1;
            string hashAlgo = readString(from);

            Path tmp = createTempDir();
            Path tmp2 = tmp + "/" + baseName;
            restorePath(tmp2, from);

            writeString(store->addToStore(tmp2, fixed, recursive, hashAlgo), to);
            
            deletePath(tmp);
            break;
        }

        case wopAddTextToStore: {
            string suffix = readString(from);
            string s = readString(from);
            PathSet refs = readStorePaths(from);
            writeString(store->addTextToStore(suffix, s, refs), to);
            break;
        }

        case wopBuildDerivations: {
            PathSet drvs = readStorePaths(from);
            store->buildDerivations(drvs);
            writeInt(1, to);
            break;
        }

        case wopEnsurePath: {
            Path path = readStorePath(from);
            store->ensurePath(path);
            writeInt(1, to);
            break;
        }

        case wopAddTempRoot: {
            Path path = readStorePath(from);
            store->addTempRoot(path);
            writeInt(1, to);
            break;
        }

        case wopSyncWithGC: {
            store->syncWithGC();
            writeInt(1, to);
            break;
        }

        default:
            throw Error(format("invalid operation %1%") % op);
        }
        
    } while (!quit);

    printMsg(lvlError, format("%1% worker operations") % opCount);
}


void run(Strings args)
{
    bool slave = false;
    bool daemon = false;
    
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;
        if (arg == "--slave") slave = true;
        if (arg == "--daemon") daemon = true;
    }

    if (slave) {
        FdSource source(STDIN_FILENO);
        FdSink sink(STDOUT_FILENO);

        /* This prevents us from receiving signals from the terminal
           when we're running in setuid mode. */
        if (setsid() == -1)
            throw SysError(format("creating a new session"));

        processConnection(source, sink);
    }

    else if (daemon)
        throw Error("daemon mode not implemented");

    else
        throw Error("must be run in either --slave or --daemon mode");
}


#include "help.txt.hh"

void printHelp()
{
    std::cout << string((char *) helpText, sizeof helpText);
}


string programId = "nix-worker";
