#include <iostream>

#include "hash.hh"
#include "shared.hh"


void run(Strings args)
{
    for (Strings::iterator it = args.begin();
         it != args.end(); it++)
        cout << format("%1%\n") % (string) hashPath(*it);
}


string programId = "nix-hash";

