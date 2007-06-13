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


/************************* Build time Functions ******************************/



/************************* Build time Functions ******************************/




void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


//
Derivation getDerivation_andCheckArgs_(Strings opFlags, Strings opArgs, Path & componentPath, Path & statePath, string & stateIdentifier, string & binary, string & derivationPath,
									   bool getDerivers, PathSet & derivers, string & username) //optional
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if ( (opArgs.size() != 1 && opArgs.size() != 2) && (getDerivers && opArgs.size() != 3) ) 
    	throw UsageError("only one or two arguments allowed component path / identiefier (or a third when you need to see the derivations: [username]) )");
    
	//Parse the full path like /nix/store/...../bin/hello
    string fullPath = opArgs.front();
    componentPath = fullPath.substr(nixStore.size() + 1, fullPath.size());		//+1 to strip off the /
    int pos = componentPath.find("/",0);
    componentPath = fullPath.substr(0, pos + nixStore.size() + 1);
    binary = fullPath.substr(pos + nixStore.size() + 1, fullPath.size());

    //TODO REAL CHECK for validity of componentPath ... ?
    if(componentPath == "/nix/store")
 		 throw UsageError("You must specify the full! binary path");
        
    if(opArgs.size() > 1){
		opArgs.pop_front();
		stateIdentifier = opArgs.front();	
    }
	
	if(username == "")
		username = getCallingUserName();  

	printMsg(lvlError, format("%1% - %2% - %3% - %4%") % componentPath % stateIdentifier % binary % username);
    
    derivers = queryDerivers(noTxn, componentPath, stateIdentifier, username);
    
    if(getDerivers == true)
    	return Derivation();
    else if(derivers.size() != 1)
    	throw UsageError("There is more than one deriver....");
        	
    Derivation drv;
    for (PathSet::iterator i = derivers.begin(); i != derivers.end(); ++i){		//ugly workaround for drvs[0].
     	derivationPath = *i;
     	drv = derivationFromPath(derivationPath);
    }
     
    DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    statePath = stateOutputs.find("state")->second.statepath;
	return drv;
}

//Wrapper
Derivation getDerivation_andCheckArgs(Strings opFlags, Strings opArgs, Path & componentPath, Path & statePath, string & stateIdentifier, string & binary, string & derivationPath)
{
	PathSet empty;
	string empty2;
	return getDerivation_andCheckArgs_(opFlags, opArgs, componentPath, statePath, stateIdentifier, binary, derivationPath, false, empty, empty2);
}

//
static void opShowDerivations(Strings opFlags, Strings opArgs)
{
	string username;
	
	if(opArgs.size() == 3)
		username = opArgs.back();
	else if(opArgs.size() == 2)
		username = getCallingUserName();
		
	string stateIdentifier;		 
	Path componentPath;
    Path statePath;
    string binary;
    PathSet derivers;
    string derivationPath;
    Derivation drv = getDerivation_andCheckArgs_(opFlags, opArgs, componentPath, statePath, stateIdentifier, binary, derivationPath, true, derivers, username);
	
	for (PathSet::iterator i = derivers.begin(); i != derivers.end(); ++i)
     	printMsg(lvlError, format("%1%") % (*i));
}


//Prints the statepath of a component - indetiefier combination
static void opShowStatePath(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string stateIdentifier;
    string binary;
    string derivationPath
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, stateIdentifier, binary, derivationPath);
	printMsg(lvlError, format("%1%") % statePath);
}

//Prints the root path that contains the repoisitorys of the state of a component - indetiefier combination
static void opShowStateReposRootPath(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string stateIdentifier;
    string binary;
    string derivationPath;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, stateIdentifier, binary, derivationPath);
	string drvName = drv.env.find("name")->second;
	
	//Get the a repository for this state location
	string repos = makeStateReposPath("stateOutput:staterepospath", statePath, "/", drvName, stateIdentifier);		//this is a copy from store-state.cc

	//TODO Strip off
	//repos = repos.substr(0, repos.length() - .... );
	
	printMsg(lvlError, format("%1%") % repos);
}


static void opRunComponent(Strings opFlags, Strings opArgs)
{
    //get the derivation of the current component
    
    Path componentPath;
    Path statePath;
    string stateIdentifier;
    string binary;
	string derivationPath
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, stateIdentifier, binary, derivationPath);
    DerivationStateOutputDirs stateOutputDirs = drv.stateOutputDirs;
    DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    DerivationOutputs outputs = drv.outputs;
    string drvName = drv.env.find("name")->second;
    
    //Check if component is a state component !!!
    
    //Check for locks ...
	//add locks ... ?
	//svn lock ... ?
	
    //******************* Run the component
    //TODO
    
	
	//******************* Afterwards, call the commit script (recursively)

    //get dependecies (if neccecary | recusively) of all state components that need to be updated
    PathSet paths = store->getStateReferencesClosure(derivationPath);


	//TODO nix-store -q --tree $(nix-store -qd /nix/store/6x6glnb9idn53yxfqrz6wq53459vv3qd-firefox-2.0.0.3/)
	
	
	//Transaction txn;
   	//createStoreTransaction(txn);
	//txn.commit();
	
	//or noTxn		
		
	
	//for(...){
    //
    //}
    
    string svnbin = nixSVNPath + "/svn";
	string svnadminbin = nixSVNPath + "/svnadmin";

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
		
		if(d.type == "interval"){
			intervalPaths.insert(fullstatedir);
		}
	}
	vector<int> intervals = store->getStatePathsInterval(intervalPaths);
	
	int intervalAt=0;	
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		DerivationStateOutputDir d = i->second;

		string thisdir = d.path;		//TODO CONVERT
		
		string fullstatedir = statePath + "/" + thisdir;
		if(thisdir == "/")									//exception for the root dir
			fullstatedir = statePath + "/";				
		
		
		//Path fullStatePath = fullstatedir;					//TODO call coerce function		//TODO REMOVE?

		if(d.type == "none"){
			nonversionedpaths.push_back(fullstatedir);
			continue;
		}
				
		//Get the a repository for this state location
		string repos = makeStateReposPath("stateOutput:staterepospath", statePath, thisdir, drvName, stateIdentifier);		//this is a copy from store-state.cc
				
		//
		checkoutcommands.push_back(svnbin + " --ignore-externals checkout file://" + repos + " " + fullstatedir);
		subversionedpaths.push_back(fullstatedir);
		
		if(d.type == "interval"){
			//Get the interval-counter from the database
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
	//store->setStatePathsInterval(intervalPaths, intervals);
		
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
		//#HACK: I cant seem to find a way for bash to parse a 2 dimensional string array as argument, so we use a 1-d array with '|' as seperator
		commandsarray += "" + *(i) + " | ";
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

	/* test *
	store = openStore();
	Path p = "/nix/store/l569q3a2cfx834mcf3vhwczjgbaljnp7-hellohardcodedstateworld-1.0";										//
	store->addUpdatedStateDerivation("/nix/store/63xcbrk3v5nbn9qla7rwnx6rvz3iqm5l-hellohardcodedstateworld-1.0.drv", p);		//
	Path p2 = "/nix/store/4ycq45hsgc8yaj4vwafx3lgd473jaqwg-hellohardcodedstateworld-1.0";
	store->addUpdatedStateDerivation("/nix/store/s6wggk924jx0gcb0l29ra4g9fxa3b4pp-hellohardcodedstateworld-1.0.drv", p2);		//
	store->updateAllStateDerivations();
	return;
	
	string a = makeStatePathFromGolbalHash("8f3b56a9a985fce54fd88c3e95a81a4b6b11fb98da12b977aee7f278c73ad3d7-hellohardcodedstateworld-1.0-test2", "kaaz");
	printMsg(lvlError, format("%1%") % a);
	return;
	
	printMsg(lvlError, format("Result: \"%1%\"") % getCallingUserName());
	return;
	*/
	store = openStore();
	//store->addUpdatedStateDerivation("/nix/store/bk4p7378ndm1p5qdr6a99wgxbiklilxy-hellohardcodedstateworld-1.0.drv", "/nix/store/vjijdazrn2jyzyk9sqwrl8fjq0qsmi8y-hellohardcodedstateworld-1.0");
	//store->updateAllStateDerivations();
	//return;
	
	PathSet paths
	storePathRequisites("", false, paths);
	
	//PathSet paths = store->getStateReferencesClosure("/nix/store/928dd2wl5cgqg10hzc3aj4rqaips6bdk-hellohardcodedstateworld-dep1-1.0.drv");
	return;
	
	/* test */
	
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;

        Operation oldOp = op;
		
        if (arg == "--run" || arg == "-r")
            op = opRunComponent;
		else if (arg == "--showstatepath")
			op = opShowStatePath;
		else if (arg == "--showstatereposrootpath")
			op = opShowStateReposRootPath;
		else if (arg == "--showderivations")
			op = opShowDerivations;

        /*
		--commit
		
		--run-without-commit
		
		--backup
		
		--exclude-commit-paths
		
		TODO update getDerivation in nix-store to handle state indentifiers
		
		--update state drv
		
		--revert-to-state	(recursive revert...)
		
		--delete state?
		
		--user=...
		
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
