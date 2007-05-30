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

//

static Derivation getDerivation_oneArgumentNoFlags(const Strings opFlags, const Strings opArgs)
{
	if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");
    string path = *opArgs.begin();
    string component = path;		//TODO Parse
    Path componentPath = component; //TODO call coerce function
	return store->getStateDerivation(componentPath);
}


//********


static void opShowStatePath(Strings opFlags, Strings opArgs)
{
	Derivation drv = getDerivation_oneArgumentNoFlags(opFlags, opArgs);
	DerivationStateOutputs stateOutputs = drv.stateOutputs; 
	Path statePath = stateOutputs.find("state")->second.statepath;
	printMsg(lvlError, format("%1%") % statePath);
}


static void opShowStateReposRootPath(Strings opFlags, Strings opArgs)
{
	Derivation drv = getDerivation_oneArgumentNoFlags(opFlags, opArgs);
	DerivationStateOutputs stateOutputs = drv.stateOutputs; 
	Path statePath = stateOutputs.find("state")->second.statepath;
	
	string drvName = drv.env.find("name")->second;
	string stateIdentifier = stateOutputs.find("state")->second.stateIdentifier;
	
	//Get the a repository for this state location
	string repos = makeStateReposPath("stateOutput:staterepospath", statePath, "", drvName, stateIdentifier);		//this is a copy from store-state.cc
	repos = repos.substr(0, repos.length() - stateRootRepos.length());
		
	printMsg(lvlError, format("%1%") % repos);
}


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

    //get the derivation of the current component
    Derivation drv = getDerivation_oneArgumentNoFlags(opFlags, opArgs);
    DerivationStateOutputDirs stateOutputDirs = drv.stateOutputDirs;
    DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    DerivationOutputs outputs = drv.outputs;
    string drvName = drv.env.find("name")->second;
	string stateIdentifier = stateOutputs.find("state")->second.stateIdentifier;
    
    
    string path = *opArgs.begin();

	//Data from user / profile
    string component = path;		//TODO Parse
    Path componentPath = component; //TODO call coerce function
    
    string identifier = "test";
    string binary = "hello";

	//Wait for locks?
	
	//Run the component
    
    
    //********************* Commit state *********************
    
    //get dependecies (if neccecary) of all state components that need to be updated
    PathSet paths = store->getStateReferencesClosure(componentPath);
    
    //get their derivations
    //...
    
    //call the bash script on all the the store-state components
    
    
	
	//******************* Call the commit script (recursively)

	//for(...){
    //
    //}
    
    string svnbin = nixSVNPath + "/svn";
	string svnadminbin = nixSVNPath + "/svnadmin";
	Path statePath = stateOutputs.find("state")->second.statepath;

	//Vector includeing all commit scripts:
	vector<string> subversionedpaths;
	vector<bool> subversionedpathsCommitBoolean;
	vector<string> nonversionedpaths;			//of type none, no versioning needed
	vector<string> checkoutcommands;
	
	//Get all the inverals from the database at once
	PathSet intervalPaths;
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		DerivationStateOutputDir d = i->second;

		string thisdir = d.path;
		string fullstatedir = statePath + "/" + thisdir;
		Path statePath = fullstatedir;					//TODO call coerce function
		
		if(d.type == "interval"){
			intervalPaths.insert(statePath);
		}
	}
	vector<int> intervals = store->getStatePathsInterval(intervalPaths);
	
	int intervalAt=0;	
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		DerivationStateOutputDir d = i->second;

		string thisdir = d.path;
		string fullstatedir = statePath + "/" + thisdir;
		Path statePath = fullstatedir;					//TODO call coerce function

		if(d.type == "none"){
			nonversionedpaths.push_back(fullstatedir);
			continue;
		}
				
		//Get the a repository for this state location
		string repos = makeStateReposPath("stateOutput:staterepospath", statePath, thisdir, drvName, stateIdentifier);		//this is a copy from store-state.cc
				
		//
		checkoutcommands.push_back(svnbin + " checkout file://" + repos + " " + fullstatedir);
		subversionedpaths.push_back(fullstatedir);
		
		if(d.type == "interval"){

			//TODO comment
			
			//Get the interval-counter from the database, and update it.
			//printMsg(lvlError, format("Interval: %1% - %2%") %  % );
			
			int interval_counter = intervals[intervalAt];
			int interval = d.getInterval();
			
			subversionedpathsCommitBoolean.push_back(interval_counter % interval == 0);
    		
			//update the interval
			intervals[intervalAt] = interval_counter + 1;
			
			intervalAt++;
		}
		else if(d.type == "full")
			subversionedpathsCommitBoolean.push_back(true);
		else if(d.type == "manual")											//TODO !!!!!
			subversionedpathsCommitBoolean.push_back(false);
		else
			throw Error(format("interval '%1%' is not handled in nix-state") % d.type);
	}
	
	//Update the intervals again
	//store->setStatePathsInterval(intervalPaths, intervals);		//TODO
		
	//Call the commit script with the appropiate paramenters
	string subversionedstatepathsarray; 
	for (vector<string>::iterator i = subversionedpaths.begin(); i != subversionedpaths.end(); ++i)
    {
		subversionedstatepathsarray += *(i) + " ";
    }
	string subversionedpathsCommitBooleansarray; 
	for (vector<bool>::iterator i = subversionedpathsCommitBoolean.begin(); i != subversionedpathsCommitBoolean.end(); ++i)
    {
		subversionedpathsCommitBooleansarray += bool2string(*i) + " ";
    }
	string nonversionedstatepathsarray; 
	for (vector<string>::iterator i = nonversionedpaths.begin(); i != nonversionedpaths.end(); ++i)
    {
		nonversionedstatepathsarray += *(i) + " ";
    }
	string commandsarray; 
	for (vector<string>::iterator i = checkoutcommands.begin(); i != checkoutcommands.end(); ++i)
    {
		commandsarray += "\\\"" + *(i) + "\\\" ";
    }
  	
    //make the call
    executeAndPrintShellCommand(nixLibexecDir + "/nix/nix-statecommit.sh " + svnbin + 
    																   " \"" + subversionedstatepathsarray + "\" " +
    																   " \"" + subversionedpathsCommitBooleansarray + "\" " +
    																   " \"" + nonversionedstatepathsarray + "\" " +
    																   " \"" + commandsarray + "\" ",
    																   "commit-script");
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
		else if (arg == "--showstatepath")
			op = opShowStatePath;
		else if (arg == "--showstatereposrootpath")
			op = opShowStateReposRootPath;

        /*
		--commit
		
		--run-without-commit
		
		--backup

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
