#include <iostream>
#include <algorithm>

#include "globals.hh"
#include "misc.hh"
#include "archive.hh"
#include "shared.hh"
#include "db.hh"
#include "util.hh"
#include "help.txt.hh"


using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


static Path gcRoot;
static int rootNr = 0;
static bool indirectRoot = false;


//Look up the references of all (runtime) dependencies that maintain have state
void commitReferencesClosure(){

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
	TableId dbReferences = nixDB.openTable("references");

	nixDB.enumTable(txn, dbReferences, referencesKeys);
    for (Paths::iterator i = referencesKeys.begin(); i != referencesKeys.end(); ++i)
    {
		printMsg(lvlError, format("NIX-STATE: `%1%'") % *i);
    }
    
    
    
    
    
    
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
		/*
        if (arg == "--realise" || arg == "-r")
            op = opRealise;
        else if (arg == "--add" || arg == "-A")
            op = opAdd;
        else if (arg == "--add-fixed")
            op = opAddFixed;
        else if (arg == "--print-fixed-path")
            op = opPrintFixedPath;
        else if (arg[0] == '-')
            opFlags.push_back(arg);
        else
            opArgs.push_back(arg);

        if (oldOp && oldOp != op)
            throw UsageError("only one operation may be specified");
        */
    }

	commitReferencesClosure();
    
    //if (!op) throw UsageError("no operation specified");

    //op(opFlags, opArgs);
}


string programId = "nix-state";
