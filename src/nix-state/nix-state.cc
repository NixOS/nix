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
    string component = "/nix/store/s0g22b5lw693cjpclypkzan27d11v5pg-hellohardcodedstateworld-1.0";
    Path componentPath = component; //TODO call coerce function
    string identifier = "test";
    string binary = "hello";

	//Wait for locks?
	
	//Run the component
    
    
    //********************* Commit state *********************
    
    //get the derivation of the current component
    Derivation drv = store->getStateDerivation(componentPath);
    DerivationStateOutputDirs stateOutputDirs = drv.stateOutputDirs;
    DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    DerivationOutputs outputs = drv.outputs;
    string drvName = drv.env.find("name")->second;
	string stateIdentifier = stateOutputs.find("state")->second.stateIdentifier;
    
    
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
	
	Path statePath = stateOutputs.find("state")->second.statepath;
	string stateDir = statePath;
	
	//Vector includeing all commit scripts:
	vector<string> commitscript;
	vector<string> subversionedpaths;
	vector<int> subversionedpathsInterval;
	vector<string> nonversionedpaths;			//of type none, no versioning needed
	vector<string> checkoutcommands;
	
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		DerivationStateOutputDir d = i->second;

		string thisdir = d.path;
		string fullstatedir = stateDir + "/" + thisdir;
		Path statePath = fullstatedir;					//TODO call coerce function

		if(d.type == "none"){
			nonversionedpaths.push_back(fullstatedir);
			continue;
		}
				
		//Get the a repository for this state location
		string repos = makeStateReposPath("stateOutput:staterepospath", stateDir, thisdir, drvName, stateIdentifier);		//this is a copy from store-state.cc
				
		//
		checkoutcommands.push_back(svnbin + " checkout file://" + repos + " " + fullstatedir);
		subversionedpaths.push_back(fullstatedir);
		
		if(d.type == "interval"){
			subversionedpathsInterval.push_back(d.getInterval());
		}
		else
			subversionedpathsInterval.push_back(0);
	}
		
	//create super commit script
	printMsg(lvlError, format("svnbin=%1%") % svnbin);
	string subversionedstatepathsarray = "subversionedpaths=( "; 
	for (vector<string>::iterator i = subversionedpaths.begin(); i != subversionedpaths.end(); ++i)
    {
		subversionedstatepathsarray += *(i) + " ";
    }
    printMsg(lvlError, format("%1%)") % subversionedstatepathsarray);
	string subversionedpathsIntervalsarray = "subversionedpathsInterval=( "; 
	for (vector<int>::iterator i = subversionedpathsInterval.begin(); i != subversionedpathsInterval.end(); ++i)
    {
		subversionedpathsIntervalsarray += int2String(*i) + " ";
    }
	printMsg(lvlError, format("%1%)") % subversionedpathsIntervalsarray);
	string nonversionedstatepathsarray = "nonversionedpaths=( "; 
	for (vector<string>::iterator i = nonversionedpaths.begin(); i != nonversionedpaths.end(); ++i)
    {
		nonversionedstatepathsarray += *(i) + " ";
    }
	printMsg(lvlError, format("%1%)") % nonversionedstatepathsarray);
	string commandsarray = "checkouts=( "; 
	for (vector<string>::iterator i = checkoutcommands.begin(); i != checkoutcommands.end(); ++i)
    {
		commandsarray += "\"" + *(i) + "\" ";
    }
	printMsg(lvlError, format("%1%)") % commandsarray);
	for (vector<string>::iterator i = commitscript.begin(); i != commitscript.end(); ++i)
    {
    	string s = *(i);
    	printMsg(lvlError, format("%1%") % s);
    }    	
	
	
	
    //svnbin=/nix/var/nix/profiles/per-user/root/profile/bin/svn
	//subversionedpaths=( /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/ )
	//subversionedpathsInterval=( 0 0 )
	//nonversionedpaths=( /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/cache/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/test/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/test2/test2/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/logging/ )
	//checkouts=( "/nix/var/nix/profiles/per-user/root/profile/bin/svn checkout file:///nix/staterepos/99dj5zg1ginj5as75nkb0psnp02krv2s-hellohardcodedstateworld-1.0 /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/" "/nix/var/nix/profiles/per-user/root/profile/bin/svn checkout file:///nix/staterepos/9ph3nd4irpvgs66h24xjvxrwpnrwy9n0-hellohardcodedstateworld-1.0 /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/" )
    
    
    //for(...){
    //
    //}

}


void run(Strings args)
{
    Strings opFlags, opArgs;
    Operation op = 0;

    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;

        Operation oldOp = op;
		
        if (arg == "--run" || arg == "-r")
            op = opCommitReferencesClosure;

        /*
		
		--commit
		
		--run-without-commit
		
		--backup
		
		--showlocation
		
		

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

    
    if (!op) throw UsageError("no operation specified");
    
    /* !!! hack */
    store = openStore();

    op(opFlags, opArgs);
}


string programId = "nix-state";
