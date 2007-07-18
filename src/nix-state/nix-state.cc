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
#include "references.hh"
#include "store-state.hh"

using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);

//two global variables
string stateIdentifier;
string username;
int revision_arg;
bool scanforReferences = false;
bool only_commit = false;


/************************* Build time Functions ******************************/



/************************* Build time Functions ******************************/



/************************* Run time Functions ******************************/

void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


Derivation getDerivation(const string & fullPath, const string & program_args, string state_identifier, Path & componentPath, Path & statePath, 
						 string & binary, string & derivationPath, bool isStateComponent,
						 bool getDerivers, PathSet & derivers) 	//optional
{
	//Parse the full path like /nix/store/...../bin/hello
    //string fullPath = opArgs.front();
    
    componentPath = fullPath.substr(nixStore.size() + 1, fullPath.size());		//+1 to strip off the /
    int pos = componentPath.find("/",0);
    componentPath = fullPath.substr(0, pos + nixStore.size() + 1);
    binary = fullPath.substr(pos + nixStore.size() + 1, fullPath.size());

    //TODO REAL CHECK for validity of componentPath ... ? sometimes, indentifier can be excluded
    if(componentPath == "/nix/store")
 		 throw UsageError("You must specify the full! binary path");
     
    //Check if path is statepath
    isStateComponent = store->isStateComponent(componentPath);
      
	//printMsg(lvlError, format("'%1%' - '%2%' - '%3%' - '%4%' - '%5%'") % componentPath % state_identifier % binary % username % program_args);
    
    if(isStateComponent)
    	derivers = queryDerivers(noTxn, componentPath, state_identifier, username);
    else
    	derivers.insert(queryDeriver(noTxn, componentPath));
    
    if(getDerivers == true)
    	return Derivation();
    
    if(isStateComponent){	
	    if(derivers.size() == 0)
	    	throw UsageError(format("There are no derivers with this combination of identifier '%1%' and username '%2%'") % state_identifier % username);
	    if(derivers.size() != 1)
	    	throw UsageError(format("There is more than one deriver with state_identifier '%1%' and username '%2%'") % state_identifier % username);
    }
    
    //Retrieve the derivation, there is only 1 drvPath in derivers
    Derivation drv = derivationFromPath(*(derivers.begin()));
	
	if(isStateComponent){
    	DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    	statePath = stateOutputs.find("state")->second.statepath;
	}
	
	return drv;
}

//Wrapper
Derivation getDerivation_andCheckArgs_(Strings opFlags, Strings opArgs, Path & componentPath, Path & statePath, 
									   string & binary, string & derivationPath, bool isStateComponent, string & program_args,
									   bool getDerivers, PathSet & derivers) 	//optional
{
	if (!opFlags.empty()) throw UsageError("unknown flag");
    if ( opArgs.size() != 1 && opArgs.size() != 2 ) 
    	throw UsageError("only one or two arguments allowed component path and program arguments (counts as one) ");
    	
   
   	string fullPath = opArgs.front(); 
   	
    if(opArgs.size() > 1){
		opArgs.pop_front();
		program_args = opArgs.front();
		//Strings progam_args_strings = tokenizeString(program_args, " ");
    }
   
    return getDerivation(fullPath, program_args, stateIdentifier, componentPath, statePath, binary, derivationPath, isStateComponent, getDerivers, derivers);	
}

//Wrapper
Derivation getDerivation_andCheckArgs(Strings opFlags, Strings opArgs, Path & componentPath, Path & statePath, 
									  string & binary, string & derivationPath, bool & isStateComponent, string & program_args)
{
	PathSet empty;
	return getDerivation_andCheckArgs_(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args, false, empty);
}

//
static void opShowDerivations(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    PathSet derivers;
    string derivationPath;
    bool isStateComponent;
    string program_args;
    Derivation drv = getDerivation_andCheckArgs_(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args, true, derivers);
	
	if(!isStateComponent)
		throw UsageError(format("This path '%1%' is not a state-component path") % componentPath);
	
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
    bool isStateComponent;
    string program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args);
    
    if(!isStateComponent)
		throw UsageError(format("This path '%1%' is not a state-component path") % componentPath);
    
	printMsg(lvlError, format("%1%") % statePath);
}

//Prints the root path that contains the repoisitorys of the state of a component - indetiefier combination
static void opShowStateReposPath(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    string derivationPath;
    bool isStateComponent;
    string program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args);
	
	if(!isStateComponent)
		throw UsageError(format("This path '%1%' is not a state-component path") % componentPath);
	
	//Get the a repository for this state location
	string repos = getStateReposPath("stateOutput:staterepospath", statePath);		//this is a copy from store-state.cc

	printMsg(lvlError, format("%1%") % repos);
}



/*
 * Input: store (or statePath?)
 * Returns all the drv's of the statePaths (in)directly referenced.
 */
PathSet getAllStateDerivationsRecursively(const Path & storePath, const int revision)
{
	//Get recursively all state paths
	PathSet statePaths;
	store->storePathRequisites(storePath, false, statePaths, false, true, revision);		
	
	//Find the matching drv with the statePath
	PathSet derivations;	
	for (PathSet::iterator i = statePaths.begin(); i != statePaths.end(); ++i)
		derivations.insert(store->queryStatePathDrv(*i));
			
	return derivations;
}

//TODO also allow statePaths as input?
static void queryAvailableStateRevisions(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    string derivationPath;
    bool isStateComponent;
    string program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args);
	
	RevisionNumbers revisions;
	bool notEmpty = store->queryAvailableStateRevisions(statePath, revisions);
	
	if(!notEmpty){	//can this happen?
		printMsg(lvlError, format("No revisions yet for: %1%") % statePath);
		return;	
	}
	
	//Sort ourselfes to create a nice output
	vector<int> revisions_sort;
	for (RevisionNumbers::iterator i = revisions.begin(); i != revisions.end(); ++i)
		revisions_sort.push_back(*i);
	sort(revisions_sort.begin(), revisions_sort.end());
	string revisions_txt="";
	for (vector<int>::iterator i = revisions_sort.begin(); i != revisions_sort.end(); ++i)
		revisions_txt += int2String(*i) + " ";
	printMsg(lvlError, format("Available Revisions: %1%") % revisions_txt);
}

static void revertToRevision(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    string derivationPath;
    bool isStateComponent;
    string program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args);
    
    bool recursive = true;	//TODO !!!!!!!!!!!!!!!!!
    
    PathSet statePaths;
    if(recursive)
    	PathSet statePaths = getAllStateDerivationsRecursively(componentPath, revision_arg);	//get dependecies (if neccecary | recusively) of all state components that need to be updated
    else
		statePaths.insert(derivationPath);	//Insert direct state path
		    	
    //Get the revisions recursively to also roll them back
    RevisionNumbersSet getRivisions;
	bool b = store->queryStateRevisions(statePath, getRivisions, revision_arg);
    
    //Sort the statePaths from all drvs
	//map<Path, string> state_repos;
	//vector<Path> sorted_paths;
    //for (PathSet::iterator d = statePaths.begin(); d != statePaths.end(); ++d){
       	
		//state_repos[statePath] = repos; 
//		sorted_paths.push_back(statePath);
	//}
	//sort(sorted_paths.begin(), sorted_paths.end());	
	
	//string repos = 
	
	//Revert each statePath in the list
	for (RevisionNumbersSet::iterator i = getRivisions.begin(); i != getRivisions.end(); ++i){
		Path statePath = (*i).first;
		int revision = (*i).second;
		string repos = getStateReposPath("stateOutput:staterepospath", statePath);		//this is a copy from store-state.cc

		printMsg(lvlError, format("Reverting statePath '%1%' to revision: %2%") % statePath % int2String(revision));
		Strings p_args;
		p_args.push_back(nixSVNPath + "/svn");
		p_args.push_back(int2String(revision));
		p_args.push_back("file://" + repos);
		p_args.push_back(statePath);
		runProgram_AndPrintOutput(nixLibexecDir + "/nix/nix-restorerevision.sh", true, p_args, "svn");	//run
		
		//TODO !!!!!!!!!!!!!!!!!!!!! do a commit
		//TODO !!!!!!!!!!!!!!!!!!!!! check if statePath is a working copy
	}
}

//TODO include this call in the validate function
//TODO ONLY CALL THIS FUNCTION ON A NON-SHARED STATE PATH!!!!!!!!!!!
void scanAndUpdateAllReferencesTxn(const Transaction & txn, const Path & statePath
								, PathSet & newFoundComponentReferences, PathSet & newFoundStateReferences, const int revision) //only for recursion
{
	//Check if is a state Path
	if(! isValidStatePathTxn(txn, statePath))
		throw Error(format("This path '%1%' is not a state path") % statePath);

	//printMsg(lvlError, format("scanAndUpdateAllReferencesTxn: '%1%' - %2%") % statePath % revision);
	
	//TODO check if path is not a shared path !
	//TODO

	//get all possible state and component references
    PathSet allComponentPaths;
    PathSet allStatePaths;
    queryAllValidPaths(txn, allComponentPaths, allStatePaths);
    
    //Remove derivation paths
	PathSet allComponentPaths2;	//without derivations
    for (PathSet::iterator i = allComponentPaths.begin(); i != allComponentPaths.end(); ++i){
    	string path = *i;
    	if(path.substr(path.length() - 4,path.length()) != ".drv")		//TODO HACK: we should have a typed table or a seperate table ....
    		allComponentPaths2.insert(path);
    }

	//TODO maybe only scan in the changeset (patch) for new references? (this will be difficult and depending on the underlying versioning system)

    //Scan in for (new) component and state references
    PathSet state_references = scanForReferences(statePath, allComponentPaths2);
    PathSet state_stateReferences = scanForStateReferences(statePath, allStatePaths);
    
    //Retrieve old references
    PathSet old_references;
    PathSet old_state_references;
    queryReferencesTxn(txn, statePath, old_references, -1);				//get the latsest references
    queryStateReferencesTxn(txn, statePath, old_state_references, -1);		
    
    //Check for added and removed paths
	PathSet diff_references_removed;
	PathSet diff_references_added;
	pathSets_difference(state_references, old_references, diff_references_removed, diff_references_added);
	PathSet diff_state_references_removed;
	PathSet diff_state_references_added;
	pathSets_difference(state_stateReferences, old_state_references, diff_state_references_removed, diff_state_references_added);

	//Set PathSet's for the caller of this function
	newFoundComponentReferences = diff_references_added;
	newFoundStateReferences = diff_state_references_added;
	
	//Print error, but we could also throw an error.
	if(diff_references_added.size() != 0)
    	for (PathSet::iterator i = diff_references_added.begin(); i != diff_references_added.end(); ++i)
    		printMsg(lvlError, format("Added component reference found!: '%1%' in state path '%2%'") % (*i) % statePath);
    if(diff_references_removed.size() != 0)
    	for (PathSet::iterator i = diff_references_removed.begin(); i != diff_references_removed.end(); ++i)
    		printMsg(lvlError, format("Removed component reference found!: '%1%' in state path '%2%'") % (*i) % statePath);
    if(diff_state_references_added.size() != 0)
    	for (PathSet::iterator i = diff_state_references_added.begin(); i != diff_state_references_added.end(); ++i)
    		printMsg(lvlError, format("Added state reference found!: '%1%' in state path '%2%'") % (*i) % statePath);
    if(diff_state_references_removed.size() != 0)
    	for (PathSet::iterator i = diff_state_references_removed.begin(); i != diff_state_references_removed.end(); ++i)
    		printMsg(lvlError, format("Removed state reference found!: '%1%' in state path '%2%'") % (*i) % statePath);

	//If any changes are detected: register the paths valid with a new revision number
   	if(diff_references_added.size() != 0 || diff_references_removed.size() != 0 ||
   	   diff_state_references_added.size() != 0 || diff_state_references_removed.size() != 0 )
   	{
	   	printMsg(lvlError, format("Updating new references to revision %1% for statepath: '%2%'") % revision % statePath);
	   	Path drvPath = queryStatePathDrvTxn(txn, statePath);
	   	registerValidPath(txn,    	
	    		statePath,
	    		Hash(),				//emtpy hash
	    		state_references,
	    		state_stateReferences,
	    		drvPath,
	    		revision);
	}
}

void scanAndUpdateAllReferencesRecusivelyTxn(const Transaction & txn, const Path & statePath)		//TODO Can also work for statePaths???
{
   	if(! isValidStatePathTxn(txn, statePath))
   		throw Error(format("This path '%1%' is not a state path") % statePath);

	//get all state current state references recursively
	PathSet statePaths;
	store->storePathRequisites(statePath, false, statePaths, false, true, -1);		//Get all current state dependencies
	
	//Add own statePath (may already be in there, but its a set, so no doubles)
	statePaths.insert(statePath);
	
   	//We dont need to sort since the db does that
	//call scanForAllReferences again on all statePaths
	for (PathSet::iterator i = statePaths.begin(); i != statePaths.end(); ++i){
		int revision = readRevisionNumber(*i);
		
		//Scan, update, call recursively
		PathSet newFoundComponentReferences;
		PathSet newFoundStateReferences;
		scanAndUpdateAllReferencesTxn(txn, *i, newFoundComponentReferences, newFoundStateReferences, revision);

		//Call the function recursively again on all newly found references												//TODO test if this works
		PathSet allNewReferences = pathSets_union(newFoundComponentReferences, newFoundStateReferences);
		for (PathSet::iterator j = allNewReferences.begin(); j != allNewReferences.end(); ++j)
			scanAndUpdateAllReferencesRecusivelyTxn(txn, *j);
	}
}



static void opRunComponent(Strings opFlags, Strings opArgs)
{
    //get the all the info of the component that is being called (we dont really use it yet)
    Path root_componentPath;
    Path root_statePath;
    string root_binary;
	string root_derivationPath;
	bool root_isStateComponent;
	string root_program_args;
    Derivation root_drv = getDerivation_andCheckArgs(opFlags, opArgs, root_componentPath, root_statePath, root_binary, root_derivationPath, root_isStateComponent, root_program_args);
        
    //Check for locks ... ? or put locks on the neseccary state components
    //WARNING: we need to watch out for deadlocks!
	//add locks ... ?
	//svn lock ... ?

	//get all current dependecies (if neccecary | recusively) of all state components that need to be updated
    PathSet statePaths;
	store->storePathRequisites(root_componentPath, false, statePaths, false, true, -1);
	statePaths.insert(root_statePath);
    
    //TODO maybe also scan the parameters for state or component hashes?
    //program_args
	
	//TODO
	Transaction txn;
   	//createStoreTransaction(txn);
	
	
	//******************* Run ****************************
	
	if(!only_commit)
		executeShellCommand(root_componentPath + root_binary + " " + root_program_args);	//more efficient way needed ???
  		
	//******************* With everything in place, we call the commit script on all statePaths (in)directly referenced **********************
	
	//Commit all statePaths
	for (PathSet::iterator i = statePaths.begin(); i != statePaths.end(); ++i)		//TODO first commit own state path?
		commitStatePathTxn(txn, *i);
	
	//Start transaction TODO

	//Scan for new references, and update with revision number
   	if(scanforReferences)
  		scanAndUpdateAllReferencesRecusivelyTxn(txn, root_statePath);

	//Get new revision number
	updateRevisionsRecursivelyTxn(txn, root_statePath);
		
	//Commit transaction
	//txn.commit();
	
	//Debugging
	RevisionNumbersSet getRivisions;
	bool b = store->queryStateRevisions(root_statePath, getRivisions, -1);
	for (RevisionNumbersSet::iterator i = getRivisions.begin(); i != getRivisions.end(); ++i){
		printMsg(lvlError, format("State %1% has revision %2%") % (*i).first % int2String((*i).second));
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
	
	Paths p1;
	p1.push_back("a");
	p1.push_back("b");
	p1.push_back("c");
	Paths p2;
	p2.push_back("b");
	p2.push_back("d");
	
	PathSet px = pathSets_union(PathSet(p1.begin(), p1.end()), PathSet(p2.begin(), p2.end()));
	
	for (PathSet::iterator i = px.begin(); i != px.end(); ++i)
		printMsg(lvlError, format("MERGED: %1%") % *i);
			
	Database nixDB;
	Path statePath = "afsdsdafsadf-sda-fsda-f-sdaf-sdaf";
	int revision = 5;
	Path statePath2;
	Path gets = nixDB.makeStatePathRevision(statePath, revision);
    int revision2;
    nixDB.splitStatePathRevision(gets, statePath2, revision2);
	printMsg(lvlError, format("'%1%' '%2%'") % statePath2 % int2String(revision2));
	
	store = openStore();
	Derivation drv = derivationFromPath("/nix/store/r2lvhrd8zhb877n07cqvcyp11j9ws5p0-hellohardcodedstateworld-dep1-1.0.drv");
	readRevisionNumbers(drv);
	
	
	Strings strings;
	strings.push_back("1");
	strings.push_back("3");
	strings.push_back("2");
	string packed = packStrings(strings);
	printMsg(lvlError, format("PA '%1%'") % packed);
	Strings strings2 = unpackStrings(packed);
	for (Strings::iterator i = strings2.begin(); i != strings2.end(); ++i)
		printMsg(lvlError, format("UN '%1%'") % *i);

	*/
	
	//updateRevisionNumbers("/nix/state/xf582zrz6xl677llr07rvskgsi3dli1d-hellohardcodedstateworld-dep1-1.0-test");
	//return;
	
	/* test */
	
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;

        Operation oldOp = op;
		
        if (arg == "--run" || arg == "-r")
            op = opRunComponent;
        else if (arg == "--commit-only"){
			op = opRunComponent;
			only_commit = true;
    	}
		else if (arg == "--showstatepath")
			op = opShowStatePath;
		else if (arg == "--showstatereposrootpath")
			op = opShowStateReposPath;
		else if (arg == "--showderivations")
			op = opShowDerivations;
		else if (arg == "--showrevisions")
			op = queryAvailableStateRevisions;
		else if (arg.substr(0,21) == "--revert-to-revision="){
			op = revertToRevision;
			bool succeed = string2Int(arg.substr(21,arg.length()), revision_arg);
			if(!succeed)
				throw UsageError("The given revision is not a valid number");
		}

        /*
		
		--commit

		--run-without-commit
		
		--showrevisions
		
		--revert-to-revision=
		
		--share-from
		
		--unshare
				
		OPTIONAL
		
		--scanreferences
								
		/////////////////////
		
		--backup ?
		
		--exclude-commit-paths
		
		TODO update getDerivation in nix-store to handle state indentifiers
		
		--delete-revision
		
        */
        
        else if (arg == "--scanreferences")
        	scanforReferences = true;        
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
