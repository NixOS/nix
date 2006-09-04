#include <iostream>

#include "hash.hh"
#include "shared.hh"
#include "help.txt.hh"


using namespace nix;


void printHelp()
{
    std::cout << string((char *) helpText, sizeof helpText);
}


void run(Strings args)
{
    HashType ht = htMD5;
    bool flat = false;
    bool base32 = false;
    bool truncate = false;
    enum { opHash, opTo32, opTo16 } op = opHash;

    Strings ss;

    for (Strings::iterator i = args.begin();
         i != args.end(); i++)
    {
        if (*i == "--flat") flat = true;
        else if (*i == "--base32") base32 = true;
        else if (*i == "--truncate") truncate = true;
        else if (*i == "--type") {
            ++i;
            if (i == args.end()) throw UsageError("`--type' requires an argument");
            ht = parseHashType(*i);
            if (ht == htUnknown)
                throw UsageError(format("unknown hash type `%1%'") % *i);
        }
        else if (*i == "--to-base16") op = opTo16;
        else if (*i == "--to-base32") op = opTo32;
        else ss.push_back(*i);
    }

    if (op == opHash) {
        for (Strings::iterator i = ss.begin(); i != ss.end(); ++i) {
            Hash h = flat ? hashFile(ht, *i) : hashPath(ht, *i);
            if (truncate && h.hashSize > 20) h = compressHash(h, 20);
            std::cout << format("%1%\n") %
                (base32 ? printHash32(h) : printHash(h));
        }
    }

    else {
        for (Strings::iterator i = ss.begin(); i != ss.end(); ++i) {
            Hash h = op == opTo16 ? parseHash32(ht, *i) : parseHash(ht, *i);
            std::cout << format("%1%\n") %
                (op == opTo16 ? printHash(h) : printHash32(h));
        }
    }
}


string programId = "nix-hash";

