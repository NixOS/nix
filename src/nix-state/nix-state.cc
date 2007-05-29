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
	Database nixDB;
	
	try {
        nixDB.open(nixDBPath);
    } catch (DbNoPermission & e) {
        printMsg(lvlTalkative, "cannot access Nix database; continuing anyway");
        //readOnlyMode = true;
        return;
    }

	Paths referencesKeys;
	Transaction txn(nixDB);
	TableId dbReferences = nixDB.openTable("statecounters");

	nixDB.enumTable(txn, dbReferences, referencesKeys);
    for (Paths::iterator i = referencesKeys.begin(); i != referencesKeys.end(); ++i)
    {
		printMsg(lvlError, format("NIX-STATE: `%1%'") % *i);
    }*/
    
    PathSet a;
    a.insert("/nix/state/m3h15msjdv1cliqdc3ijj906dzhsf6p0-hellohardcodedstateworld-1.0/log/");
    store->getStatePathsInterval(a);
    
    
    /*
    Transaction txn;
    createStoreTransaction(txn);
    for (DerivationOutputs::iterator i = drv.outputs.begin(); 
         i != drv.outputs.end(); ++i)
    {
        registerValidPath(txn, i->second.path,
            contentHashes[i->second.path],
            allReferences[i->second.path],
            drvPath);
    }
    txn.commit();
    */
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
