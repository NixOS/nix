#include "shared.hh"
#include "local-store.hh"
#include "util.hh"
#include "serialise.hh"
#include "worker-protocol.hh"

using namespace nix;


void processConnection(Source & from, Sink & to)
{
    store = boost::shared_ptr<StoreAPI>(new LocalStore(true));

    unsigned int magic = readInt(from);
    if (magic != WORKER_MAGIC_1) throw Error("protocol mismatch");

    writeInt(WORKER_MAGIC_2, to);

    debug("greeting exchanged");

    bool quit = false;
    
    do {
        
        WorkerOp op = (WorkerOp) readInt(from);

        switch (op) {

        case wopQuit:
            /* Close the database. */
            store.reset((StoreAPI *) 0);
            writeInt(1, to);
            quit = true;
            break;

        case wopIsValidPath: {
            Path path = readString(from);
            assertStorePath(path);
            writeInt(store->isValidPath(path), to);
            break;
        }

        default:
            throw Error("invalid operation");
        }
        
    } while (!quit);
}


void run(Strings args)
{
    bool slave = false;
    bool daemon = false;
    
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;
        if (arg == "--slave") slave = true;
    }

    if (slave) {
        FdSource source(STDIN_FILENO);
        FdSink sink(STDOUT_FILENO);
        processConnection(source, sink);
    }

    else if (daemon)
        throw Error("daemon mode not implemented");

    else
        throw Error("must be run in either --slave or --daemon mode");
        
}


void printHelp()
{
}


string programId = "nix-store";
