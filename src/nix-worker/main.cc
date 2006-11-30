#include "shared.hh"
#include "local-store.hh"
#include "util.hh"
#include "serialise.hh"

using namespace nix;


void processConnection(Source & from, Sink & to)
{
    store = boost::shared_ptr<StoreAPI>(new LocalStore(true));

    unsigned int magic = readInt(from);
    if (magic != 0x6e697864) throw Error("protocol mismatch");

    writeInt(0x6478696e, to);

    debug("greeting exchanged");
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
