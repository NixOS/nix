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
#include "store-api.hh"			//TODO REMOVE

using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);

//two global variables
string stateIdentifier;
string username;


/************************* Build time Functions ******************************/



/************************* Build time Functions ******************************/



/************************* Run time Functions ******************************/

void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


//
Derivation getDerivation_andCheckArgs_(Strings opFlags, Strings opArgs, Path & componentPath, Path & statePath, 
									   string & binary, string & derivationPath, bool isStatePath, string & program_args,
									   bool getDerivers, PathSet & derivers) 	//optional
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if ( opArgs.size() != 1 && opArgs.size() != 2 ) 
    	throw UsageError("only one or two arguments allowed component path and program arguments (counts as one) ");
    
	//Parse the full path like /nix/store/...../bin/hello
    string fullPath = opArgs.front();
    componentPath = fullPath.substr(nixStore.size() + 1, fullPath.size());		//+1 to strip off the /
    int pos = componentPath.find("/",0);
    componentPath = fullPath.substr(0, pos + nixStore.size() + 1);
    binary = fullPath.substr(pos + nixStore.size() + 1, fullPath.size());

    //TODO REAL CHECK for validity of componentPath ... ?
    if(componentPath == "/nix/store")
 		 throw UsageError("You must specify the full! binary path");
     
    //Check if path is statepath
    isStatePath = store->isStateComponent(componentPath);
      
    //Extract the program arguments
    string allArgs;
    if(opArgs.size() > 1){
		opArgs.pop_front();
		allArgs = opArgs.front();
		
		program_args = allArgs;
		//Strings progam_args_strings = tokenizeString(allArgs, " ");
    }

	printMsg(lvlError, format("'%1%' - '%2%' - '%3%' - '%4%' - '%5%'") % componentPath % stateIdentifier % binary % username % allArgs);
    
    if(isStatePath)
    	derivers = queryDerivers(noTxn, componentPath, stateIdentifier, username);
    else
    	derivers.insert(queryDeriver(noTxn, componentPath));
    
    if(getDerivers == true)
    	return Derivation();
    
    if(isStatePath){	
	    if(derivers.size() == 0)
	    	throw UsageError(format("There are no derivers with this combination of identifier '%1%' and username '%2%'") % stateIdentifier % username);
	    if(derivers.size() != 1)
	    	throw UsageError(format("There is more than one deriver with stateIdentifier '%1%' and username '%2%'") % stateIdentifier % username);
    }
        	
    Derivation drv;
    for (PathSet::iterator i = derivers.begin(); i != derivers.end(); ++i){		//ugly workaround for drvs[0].
     	derivationPath = *i;
     	drv = derivationFromPath(derivationPath);
    }
	
	if(isStatePath){
    	DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    	statePath = stateOutputs.find("state")->second.statepath;
	}
	
	return drv;
}

//Wrapper
Derivation getDerivation_andCheckArgs(Strings opFlags, Strings opArgs, Path & componentPath, Path & statePath, 
									  string & binary, string & derivationPath, bool & isStatePath, string & program_args)
{
	PathSet empty;
	return getDerivation_andCheckArgs_(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStatePath, program_args, false, empty);
}

//
static void opShowDerivations(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    PathSet derivers;
    string derivationPath;
    bool isStatePath;
    string program_args;
    Derivation drv = getDerivation_andCheckArgs_(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStatePath, program_args, true, derivers);
	
	if(!isStatePath)
		throw UsageError(format("This path '%1%' is not a state path") % componentPath);
	
	for (PathSet::iterator i = derivers.begin(); i != derivers.end(); ++i)
     	printMsg(lvlError, format("%1%") % (*i));
}


//Prints the statepath of a component - indetiefier combination
static void opShowStatePath(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    string derivationPath;
    bool isStatePath;
    string program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStatePath, program_args);
    
    if(!isStatePath)
		throw UsageError(format("This path '%1%' is not a state path") % componentPath);
    
	printMsg(lvlError, format("%1%") % statePath);
}

//Prints the root path that contains the repoisitorys of the state of a component - indetiefier combination
static void opShowStateReposRootPath(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    string derivationPath;
    bool isStatePath;
    string program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStatePath, program_args);
	
	if(!isStatePath)
		throw UsageError(format("This path '%1%' is not a state path") % componentPath);
	
	//Get the a repository for this state location
	string drvName = drv.env.find("name")->second;
	string repos = getStateReposPath("stateOutput:staterepospath", statePath, "/", drvName, stateIdentifier);		//this is a copy from store-state.cc

	printMsg(lvlError, format("%1%") % repos);
}

//Comment TODO
PathSet getAllStateDerivationsRecursively(const Path & storePath)
{
	//Get recursively all state paths
	PathSet statePaths;
	store->storePathRequisites(storePath, false, statePaths, false, true);		
	
	//Find the matching drv with the statePath
	PathSet derivations;	
	for (PathSet::iterator i = statePaths.begin(); i != statePaths.end(); ++i)
		derivations.insert(store->queryStatePathDrv(*i));
			
	return derivations;
}

static void opRunComponent(Strings opFlags, Strings opArgs)
{
    //get the all the info of the component that is being called (we dont really use it yet)
    Path componentPath;
    Path statePath;
    string binary;
	string derivationPath;
	bool isStatePath;
	string program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStatePath, program_args);
        
    //Specifiy the SVN binarys
    string svnbin = nixSVNPath + "/svn";
	string svnadminbin = nixSVNPath + "/svnadmin";
    
        
    //Check for locks ... ? or put locks on the neseccary state components
    //WARNING: we need to watch out for deadlocks!
	//add locks ... ?
	//svn lock ... ?

    //get dependecies (if neccecary | recusively) of all state components that need to be updated
    PathSet drvs = getAllStateDerivationsRecursively(componentPath);
    
    
    //TODO maybe also scan the parameters for state or component hashes?
    //program_args
	
	//????	
	//Transaction txn;
   	//createStoreTransaction(txn);
	//txn.commit();
	
	//******************* Run ****************************
	
	executeShellCommand(componentPath + binary + " " + program_args);	//more efficient way needed ???
  		
	//******************* With everything in place, we call the commit script on all statePaths **********************
	
	for (PathSet::iterator d = drvs.begin(); d != drvs.end(); ++d)
	{
		//Extract the neccecary info from each Drv
		Path drvPath = *d;
       	Derivation drv = derivationFromPath(drvPath);
       	DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    	Path statePath = stateOutputs.find("state")->second.statepath;
	    DerivationStateOutputDirs stateOutputDirs = drv.stateOutputDirs;
	    string drvName = drv.env.find("name")->second; 
	
		//Print
		printMsg(lvlError, format("Committing statePath: %1%") % statePath);
	
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
			
			if(d.type == "none"){
				nonversionedpaths.push_back(fullstatedir);
				continue;
			}
					
			//Get the a repository for this state location
			string repos = getStateReposPath("stateOutput:staterepospath", statePath, thisdir, drvName, stateIdentifier);		//this is a copy from store-state.cc
					
			//Add the checkout command in case its needed
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
		//store->setStatePathsInterval(intervalPaths, intervals);	//TODO UNCOMMENT
			
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
	    Strings p_args;
		p_args.push_back(svnbin);
		p_args.push_back(subversionedstatepathsarray);
		p_args.push_back(subversionedpathsCommitBooleansarray);
		p_args.push_back(nonversionedstatepathsarray);
		p_args.push_back(commandsarray);
		runProgram_AndPrintOutput(nixLibexecDir + "/nix/nix-statecommit.sh", true, p_args, "svn");
	    																   
	   	//TODO
	   	//Scan again??
	   	//scanAndUpdateAllReferencesRecusively ...
	}
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
	printMsg(lvlTalkative, format("%1%") % a);
	return;
	
	printMsg(lvlTalkative, format("Result: \"%1%\"") % getCallingUserName());
	return;
	
	store = openStore();
	store->addStateDeriver("/nix/store/0a151npn1aps8w75kpz2zm1yl3v11kbr-hellostateworld-1.0.drv", "/nix/store/k4v52ql98x2m09sb5pz7w1lrd4hamsm0-hellostateworld-1.0");
	store->addStateDeriver("/nix/store/2hpx60ibdfv2pslg4rjvp177frijamvi-hellostateworld-1.0.drv", "/nix/store/k4v52ql98x2m09sb5pz7w1lrd4hamsm0-hellostateworld-1.0");
	return;
		
	store = openStore();
	printMsg(lvlError, format("1: %1%") % bool2string( store->isStateComponent("/nix/store/7xkw5fkz5yw7dpx0pc6l12bh9a56135c-hellostateworld-1.0") ) );
	printMsg(lvlError, format("2: %1%") % bool2string( store->isStateComponent("/nix/store/05441jm8xmsidqm43ivk0micckf0mr2m-nvidiaDrivers") ) );
	printMsg(lvlError, format("3: %1%") % bool2string( store->isStateDrvPath("/nix/store/2hpx60ibdfv2pslg4rjvp177frijamvi-hellostateworld-1.0.drv") ) );
	
	store = openStore();
	Path p = store->queryStatePathDrv("/nix/state/6g6kfgimz8szznlshf13s29fn01zp99d-hellohardcodedstateworld-1.0-test2");
	printMsg(lvlError, format("Result: %1%") % p);
	return;

	string path = "afddsafsdafsdaf.drv";
	printMsg(lvlError, format("Result: %1%") % path.substr(path.length() - 4,path.length()));

	printMsg(lvlError, format("AA: %1%") % isStorePath("/nix/store/hbxqq4d67j2y21xzp7yp01qjfkcjjbc7-hellohardcodedstateworld-1.0"));
	printMsg(lvlError, format("AA: %1%") % isStorePath("/nix/state/0qhlpz1ji4gvg3j6nk5vkcddmi3m5x1r-hellohardcodedstateworld-1.0-test2"));
	printMsg(lvlError, format("AA: %1%") % isStatePath("/nix/store/hbxqq4d67j2y21xzp7yp01qjfkcjjbc7-hellohardcodedstateworld-1.0"));
	printMsg(lvlError, format("AA: %1%") % isStatePath("/nix/state/0qhlpz1ji4gvg3j6nk5vkcddmi3m5x1r-hellohardcodedstateworld-1.0-test2"));

	PathSet p1;
	PathSet p2;
	PathSet p3;
	PathSet p4;
	p1.insert("a");
	p1.insert("c");		//old
	p1.insert("b");
	p2.insert("b");
	p2.insert("a");
	p2.insert("cc");	//new
	p2.insert("x");		//new
	pathSets_difference(p1,p2,p3,p4);
	pathSets_union(p1,p2);

	store->scanForAllReferences("/nix/state/i06flm2ahq5s0x3633z30dnav9f1wkb5-hellohardcodedstateworld-dep1-1.0-test");

	store = openStore();
	//setReferences_statePath("/nix/state/afsdsdafsdaf-sdaf", 7);
	return;
		
	*/
	
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
		
		--backup ?
		
		--exclude-commit-paths
		
		TODO update getDerivation in nix-store to handle state indentifiers
		
		--revert-to-state	(recursive revert...)
		
		--delete-state
		
		--share-from
		
        */
        
        else if (arg.substr(0,13) == "--identifier=")
        	stateIdentifier = arg.substr(13,arg.length());
        else if (arg.substr(0,7) == "--user=")
        	username = arg.substr(7,arg.length());
        else
            opArgs.push_back(arg);

        if (oldOp && oldOp != op)
            throw UsageError("only one operation may be specified");
    }
    
    if(username == "")
		username = getCallingUserName();

    if (!op) throw UsageError("no operation specified");
    
    /* !!! hack */
    store = openStore();

    op(opFlags, opArgs);
}


string programId = "nix-state";
