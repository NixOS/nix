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
    
    for (Strings::iterator i = args.begin();
         i != args.end(); i++)
    {
        if (*i == "--flat") flat = true;
        else if (*i == "--type") {
            ++i;
            if (i == args.end()) throw UsageError("`--type' requires an argument");
            if (*i == "md5") ht = htMD5;
            else if (*i == "sha1") ht = htSHA1;
            else if (*i == "sha256") ht = htSHA256;
            else throw UsageError(format("unknown hash type `%1%'") % *i);
        }
        else
            cout << format("%1%\n") % printHash(
                (flat ? hashFile(*i, ht) : hashPath(*i, ht)));
    }
}


string programId = "nix-hash";

