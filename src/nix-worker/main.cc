#include "shared.hh"
#include "local-store.hh"
#include "util.hh"

using namespace nix;


/* !!! Mostly cut&pasted from util/archive.hh */
/* Use buffered reads. */
static unsigned int readInt(int fd)
{
    unsigned char buf[8];
    readFull(fd, buf, sizeof(buf));
    if (buf[4] || buf[5] || buf[6] || buf[7])
        throw Error("implementation cannot deal with > 32-bit integers");
    return
        buf[0] |
        (buf[1] << 8) |
        (buf[2] << 16) |
        (buf[3] << 24);
}


void processConnection(int fdFrom, int fdTo)
{
    store = openStore();

    unsigned int magic = readInt(fdFrom);
    if (magic != 0x6e697864) throw Error("protocol mismatch");

    
    
}


void run(Strings args)
{
    bool slave = false;
    bool daemon = false;
    
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;
        if (arg == "--slave") slave = true;
    }

    if (slave)
        processConnection(STDIN_FILENO, STDOUT_FILENO);

    else if (daemon)
        throw Error("daemon mode not implemented");

    else
        throw Error("must be run in either --slave or --daemon mode");
        
}


void printHelp()
{
}


string programId = "nix-store";
