#include <iostream>

#include "hash.hh"
#include "shared.hh"


void run(Strings args)
{
    bool flat = false;
    for (Strings::iterator i = args.begin();
         i != args.end(); i++)
        if (*i == "--flat") flat = true;
        else
            cout << format("%1%\n") % (string) 
                (flat ? hashFile(*i) : hashPath(*i));
}


string programId = "nix-hash";

