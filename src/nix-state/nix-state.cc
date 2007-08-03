#include <iostream>
#include <algorithm>
#include <sys/time.h>

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
static void opShowStatePathAtRevision(Strings opFlags, Strings opArgs)
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
	
	//TODO
	
	//printMsg(lvlError, format("%1%") % repos);
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
    
    //TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! add TXN here ???????????
    
    PathSet statePaths;
    if(recursive)
    	PathSet statePaths = getAllStateDerivationsRecursively(componentPath, revision_arg);	//get dependecies (if neccecary | recusively) of all state components that need to be updated
    else
		statePaths.insert(derivationPath);	//Insert direct state path
		    	
    //Get the revisions recursively to also roll them back
    RevisionClosure getRivisions;
    RevisionClosureTS getTimestamps;
	bool b = store->queryStateRevisions(statePath, getRivisions, getTimestamps, revision_arg);
    
	//Revert each statePath in the list
	for (RevisionClosure::iterator i = getRivisions.begin(); i != getRivisions.end(); ++i){
		Path statePath = (*i).first;
		Snapshots revisioned_paths = (*i).second;
		int timestamp = getTimestamps[statePath];
		
		//get new timestamp (just before restoring the path) for references update ???
		
		
		//get its derivation-state-items
		Derivation statePath_drv = derivationFromPath(queryStatePathDrvTxn(noTxn, statePath));
		DerivationStateOutputDirs stateOutputDirs = statePath_drv.stateOutputDirs; 
		
		//TODO Sort snapshots??? eg first restore root, then the subdirs??
		
		for (Snapshots::iterator j = revisioned_paths.begin(); j != revisioned_paths.end(); ++j){
			Path revertPathOrFile = (*j).first;
			unsigned int epoch = (*j).second;
			
			//printMsg(lvlError, format("MAYBE '%1%'") % revertPathOrFile);
			
			//Look up the type from the drv with for the current snapshotted path
			Path statePath_postfix = revertPathOrFile.substr(nixStoreState.length() + 1, revertPathOrFile.length() - nixStoreState.length());
			statePath_postfix = statePath_postfix.substr(statePath_postfix.find_first_of("/") + 1, statePath_postfix.length());
			if(statePath_postfix == "")
				statePath_postfix = "/";
			string type = stateOutputDirs.at(statePath_postfix).type;
			if(type == "none")
				continue;

			//Now that were still here, we need to copy the state from the previous version back
			Strings p_args;
			p_args.push_back("-c");		//we use the shell to execute the cp command becuase the shell expands the '*' 
			string cpcommand = "cp";
			if(revertPathOrFile.substr(revertPathOrFile.length() -1 , revertPathOrFile.length()) == "/"){		//is dir
				string revert_to_path = revertPathOrFile.substr(0, revertPathOrFile.length() -1) + "@" + unsignedInt2String(epoch);
				cpcommand += " -R " + revert_to_path + "/*";
				
				//clean all contents of the folder first (so were sure the path is clean)
				if(pathExists(revertPathOrFile))
					deletePath(revertPathOrFile);
				
				if(epoch == 0)		//Path was deleted so were done
					continue;
				else				//If path was not deleted in the previous version, we need to make sure it exists or cp will fail
					ensureDirExists(revertPathOrFile);
				
				//If the the dir has not contents then a cp ..../* will error since * cannot be expanded. So in this case were done and dont have to revert.
				Strings p2_args;
				p2_args.push_back("-A");
				p2_args.push_back(revert_to_path + "/");
				string output = runProgram("ls", true, p2_args);
				if(output == "")
					continue;
			}
			else{																//is file
				cpcommand += " " + (revertPathOrFile + "@" + unsignedInt2String(epoch));
				
				if(epoch == 0){
					//delete file
					if(FileExist(revertPathOrFile))
						deletePath(revertPathOrFile);	//we only delete if the cp doesnt overwrite it below
					continue;
				}
			}
			
			//Revert
			printMsg(lvlError, format("Reverting '%1%@%2%'") % revertPathOrFile % unsignedInt2String(epoch));
			printMsg(lvlError, format("Command: '%1%'") % cpcommand);
			cpcommand += " " + revertPathOrFile;
			p_args.push_back(cpcommand);
			runProgram_AndPrintOutput("sh", true, p_args, "sh-cp");			//TODO does this work on windows?
		}
		
		
		//Also revert state references to the specific revision (the revision is already converted to a timestamp here)
		PathSet state_stateReferences;
	    queryStateReferencesTxn(noTxn, statePath, state_stateReferences, -1, timestamp);
    	
    	PathSet state_references;
    	queryReferencesTxn(noTxn, statePath, state_references, -1, timestamp);
    	
    	//nixDB.setStateReferences(txn, dbStateComponentReferences, dbStateRevisions, statePath, ..., -1, NEWTIMESTAMP);
		//nixDB.setStateReferences(txn, dbStateStateReferences, dbStateRevisions, statePath, ........, -1, NEWTIMESTAMP);
		
		//TODO SET REFERRERS ALSO BACK !!!!!!!!!!
		//setReferences is based on a revision, but make can change that ??
    	
		printMsg(lvlError, format("Reverted state of '%1%' to revision '%2%'") % statePath % revision_arg);
	}
	
	
    
    	
	
}

//TODO include this call in the validate function
//TODO ONLY CALL THIS FUNCTION ON A NON-SHARED STATE PATH!!!!!!!!!!!
void scanAndUpdateAllReferencesTxn(const Transaction & txn, const Path & statePath
								, PathSet & newFoundComponentReferences, PathSet & newFoundStateReferences) //only for recursion
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
   	//if(diff_references_added.size() != 0 || diff_references_removed.size() != 0 ||
   	//diff_state_references_added.size() != 0 || diff_state_references_removed.size() != 0 )

	//We always set the referernces so we know they were scanned (maybe the same) at a certain time
	if(true)
   	{
	   	printMsg(lvlError, format("Updating new references for statepath: '%1%'") % statePath);
	   	Path drvPath = queryStatePathDrvTxn(txn, statePath);
	   	registerValidPath(txn,    	
	    		statePath,
	    		Hash(),				//emtpy hash
	    		state_references,
	    		state_stateReferences,
	    		drvPath,
	    		-1);				//Set at a new timestamp
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
		
		//Scan, update, call recursively
		PathSet newFoundComponentReferences;
		PathSet newFoundStateReferences;
		scanAndUpdateAllReferencesTxn(txn, *i, newFoundComponentReferences, newFoundStateReferences);

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

    //TODO maybe also scan the parameters for state or component hashes?
    //program_args
	
	//TODO
	Transaction txn;
   	//createStoreTransaction(txn);
		
	//******************* Run ****************************
	
	if(!only_commit)
		executeShellCommand(root_componentPath + root_binary + " " + root_program_args);
  		
	//******************* Scan for new references if neccecary
   	if(scanforReferences)
  		scanAndUpdateAllReferencesRecusivelyTxn(txn, root_statePath);

	//get all current (maybe updated by the scan) dependecies (if neccecary | recusively) of all state components that need to be updated
    PathSet statePaths;
	store->storePathRequisites(root_componentPath, false, statePaths, false, true, -1);
	statePaths.insert(root_statePath);

	//Start transaction TODO
    
    //Replace all shared paths in the set for their real paths 
    statePaths = toNonSharedPathSetTxn(noTxn, statePaths);
	
	//******************* With everything in place, we call the commit script on all statePaths (in)directly referenced **********************
	
	//Commit all statePaths
	RevisionClosure rivisionMapping;
	for (PathSet::iterator i = statePaths.begin(); i != statePaths.end(); ++i)		//TODO first commit own state path?
		rivisionMapping[*i] = commitStatePathTxn(txn, *i);
	
	//Save new revisions
	setStateRevisionsTxn(txn, rivisionMapping);
	
	//Commit transaction
	//txn.commit();
	
	//Debugging
	RevisionClosure getRivisions;
	RevisionClosureTS empty;
	bool b = store->queryStateRevisions(root_statePath, getRivisions, empty, -1);
	for (RevisionClosure::iterator i = getRivisions.begin(); i != getRivisions.end(); ++i){
		//printMsg(lvlError, format("State %1% has revision %2%") % (*i).first % int2String((*i).second));
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

	//updateRevisionNumbers("/nix/state/xf582zrz6xl677llr07rvskgsi3dli1d-hellohardcodedstateworld-dep1-1.0-test");
	//return;
	
	//auto sort
	map<string, string> test;
	test["q"] = "324";
	test["c"] = "3241";
	test["a"] = "a";
	for (map<string, string>::const_iterator j = test.begin(); j != test.end(); ++j)
		printMsg(lvlError, format("KEY: '%1%'") % (*j).first);
	printMsg(lvlError, format("NOW: '%1%'") % getTimeStamp());
	return;

	map<string, int> test;
	test["a"] = 1;
	test["b"] = 2;
	printMsg(lvlError, format("NOW: '%1%'") % test["q"]);
	return;	
	
	*/

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
		--run-without-commit

		--show-revision-path=....
		
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
