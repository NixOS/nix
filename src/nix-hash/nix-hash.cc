#include <iostream>

#include "hash.hh"
#include "shared.hh"
#include "help.txt.hh"


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


void run(Strings args)
{
    HashType ht = htMD5;
    bool flat = false;
    bool base32 = false;
    
    for (Strings::iterator i = args.begin();
         i != args.end(); i++)
    {
        if (*i == "--flat") flat = true;
        else if (*i == "--base32") base32 = true;
        else if (*i == "--type") {
            ++i;
            if (i == args.end()) throw UsageError("`--type' requires an argument");
            ht = parseHashType(*i);
            if (ht == htUnknown)
                throw UsageError(format("unknown hash type `%1%'") % *i);
        }
        else {
            Hash h = flat ? hashFile(ht, *i) : hashPath(ht, *i);
            cout << format("%1%\n") %
                (base32 ? printHash32(h) : printHash(h));
        }
    }
}


string programId = "nix-hash";

