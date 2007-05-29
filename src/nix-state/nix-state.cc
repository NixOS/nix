#include <iostream>
#include <algorithm>

#include "globals.hh"
#include "misc.hh"
#include "archive.hh"
#include "shared.hh"
#include "db.hh"
#include "util.hh"
#include "help.txt.hh"
#include "local-store.hh"
#include "derivations.hh"

using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


//Look up the references of all (runtime) dependencies that maintain have state

static void opCommitReferencesClosure(Strings opFlags, Strings opArgs)
{
	/*
	Paths referencesKeys;
	Transaction txn(nixDB);
	TableId dbReferences = nixDB.openTable("statecounters");

	nixDB.enumTable(txn, dbReferences, referencesKeys);
    for (Paths::iterator i = referencesKeys.begin(); i != referencesKeys.end(); ++i)
    {
		printMsg(lvlError, format("NIX-STATE: `%1%'") % *i);
    }*/

	//Data from user / profile
    string component = "/nix/store/1hyp7iiiig3rdf99y74yqhi2jkfpa8pf-hellohardcodedstateworld-1.0";
    Path componentPath = component; //TODO call coerce function
    string identifier = "test";
    string binary = "hello";

	//Wait for locks?
	
	//Run the component
    
    
    //********************* Commit state *********************
    
    //get the derivation
    Derivation drv = store->getStateDerivation(componentPath);
    
    DerivationStateOutputDirs stateOutputDirs;
    DerivationStateOutputs stateOutputs; 
    
    //get dependecies (if neccecary) of all state components that need to be updated
    PathSet paths = store->getStateReferencesClosure(componentPath);
    
    //get their derivations
    //...
    
    //Get and update the intervals
    //store->getStatePathsInterval(a);
    //store->setStatePathsInterval(a);
        
    //call the bash script on all the the store-state components
    
    string svnbin = nixSVNPath + "/svn";
	string svnadminbin = nixSVNPath + "/svnadmin";
	
    //svnbin=/nix/var/nix/profiles/per-user/root/profile/bin/svn
	//subversionedpaths=( /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/ )
	//subversionedpathsInterval=( 0 0 )
	//nonversionedpaths=( /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/cache/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/test/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/test2/test2/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/logging/ )
	//checkouts=( "/nix/var/nix/profiles/per-user/root/profile/bin/svn checkout file:///nix/staterepos/99dj5zg1ginj5as75nkb0psnp02krv2s-hellohardcodedstateworld-1.0 /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/" "/nix/var/nix/profiles/per-user/root/profile/bin/svn checkout file:///nix/staterepos/9ph3nd4irpvgs66h24xjvxrwpnrwy9n0-hellohardcodedstateworld-1.0 /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/" )
    
    
    //for(...){
    //
    //}

}



//Call the appropiate commit scripts with variables like interval





/* Scan the arguments; find the operation, set global flags, put all
   other flags in a list, and put all other arguments in another
   list. */

void run(Strings args)
{
    Strings opFlags, opArgs;
    Operation op = 0;

    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;

        Operation oldOp = op;
		
        if (arg == "--start" || arg == "-r")
            op = opCommitReferencesClosure;
        /*
        else if (arg == "--add" || arg == "-A")
            op = opAdd;
        else if (arg == "--add-fixed")
            op = opAddFixed;
        else if (arg == "--print-fixed-path")
            op = opPrintFixedPath;
        else if (arg[0] == '-')
            opFlags.push_back(arg);
        */
        else
            opArgs.push_back(arg);

        if (oldOp && oldOp != op)
            throw UsageError("only one operation may be specified");
    }

	//opCommitReferencesClosure();
    
    if (!op) throw UsageError("no operation specified");
    
    /* !!! hack */
    store = openStore();

    op(opFlags, opArgs);
}


string programId = "nix-state";
