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
    bool flat = false;
    for (Strings::iterator i = args.begin();
         i != args.end(); i++)
        if (*i == "--flat") flat = true;
        else
            cout << format("%1%\n") % (string) 
                (flat ? hashFile(*i) : hashPath(*i));
}


string programId = "nix-hash";

