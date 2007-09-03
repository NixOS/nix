#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fstream>
#include <iostream>

#include "store-state.hh"
#include "globals.hh"
#include "util.hh"
#include "derivations.hh"
#include "store-api.hh"
#include "local-store.hh"
#include "misc.hh"
#include "archive.hh"
#include "snapshot.hh"
#include "references.hh"

namespace nix {

/*
void updatedStateDerivation(Path storePath)
{
	//We dont remove the old .svn folders
	//update in database?
		
	printMsg(lvlTalkative, format("Resetting state drv settings"));
}
*/

void createSubStateDirsTxn(const Transaction & txn, const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs)
{
	Path statePath = stateOutputs.find("state")->second.statepath;
	string stateDir = statePath;
	
	PathSet intervalPaths;

	//check if we can create state dirs
	//TODO
	
	/*
	if( ! IsDirectory( ....... ) ){
	}
	else
		printMsg(lvlTalkative, format("Statedir %1% already exists, so dont ........ ???? ") % ...);
	*/	
	
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		DerivationStateOutputDir d = i->second;

		string thisdir = d.path;
		Path fullstatedir = stateDir + "/" + thisdir;
		
		//If it is a file: continue
		if(thisdir.substr(thisdir.length() -1 , thisdir.length()) != "/"){
			continue;
		}		
		
		ensureDirExists(fullstatedir);
		
		if(d.type == "interval"){
			intervalPaths.insert(fullstatedir);
		}		
	}
	
	setChown(statePath, queryCallingUsername(), "nixbld", true);		//Set all dirs in the statePath recursively to their owners
	printMsg(lvlTalkative, format("Set CHOWN '%1%'") % (statePath + "-" + queryCallingUsername()));
	
	//Initialize the counters for the statePaths that have an interval to 0
	IntVector empty;
	setStatePathsIntervalTxn(txn, intervalPaths, empty, true);
}

/*
 * Input: store (or statePath?)
 * Returns all the drv's of the statePaths (in)directly referenced.
 */
 //TODO TXN
PathSet getAllStateDerivationsRecursivelyTxn(const Transaction & txn, const Path & storePath, const int revision)
{
	//Get recursively all state paths
	PathSet statePaths;
	storePathRequisitesTxn(noTxn, storePath, false, statePaths, false, true, revision);		
	
	//Find the matching drv with the statePath
	PathSet derivations;	
	for (PathSet::iterator i = statePaths.begin(); i != statePaths.end(); ++i)
		derivations.insert(queryStatePathDrvTxn(txn,*i));
			
	return derivations;
}



void revertToRevisionTxn(const Transaction & txn, const Path & statePath, const int revision_arg, const bool recursive)
{
	//Unshare the path
	Path statePath_ns = toNonSharedPathTxn(txn, statePath);
	
	//get a new timestamp for the references update
	unsigned int newTimestamp = getTimeStamp();
		    	
    //Get the revisions recursively to also roll them back
    RevisionClosure getRivisions;
    RevisionClosureTS getTimestamps;
	queryStateRevisionsTxn(txn, statePath_ns, getRivisions, getTimestamps, revision_arg);

	//Revert each statePath in the list
	for (RevisionClosure::iterator i = getRivisions.begin(); i != getRivisions.end(); ++i){
		Path statePath = (*i).first;
		Snapshots revisioned_paths = (*i).second;
		unsigned int timestamp = getTimestamps[statePath];
		
		//get its derivation-state-items
		Derivation statePath_drv = derivationFromPathTxn(txn, queryStatePathDrvTxn(noTxn, statePath));
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
			runProgram_AndPrintOutput("sh", true, p_args, "sh-cp");
		}
		
		
		//*** Now also revert state references to the specific revision (the revision is already converted to a timestamp here)
		
		//Query the references of the old revision (already converted to a timestamp)		
    	PathSet state_references;
    	queryXReferencesTxn(txn, statePath, state_references, true, 0, timestamp);
		PathSet state_stateReferences;
	    queryXReferencesTxn(txn, statePath, state_stateReferences, false, 0, timestamp);
    	
    	//Now set these old references as the new references at the new (just created) Timestamp
    	setStateComponentReferencesTxn(txn, statePath, Strings(state_references.begin(), state_references.end()), 0, newTimestamp);
		setStateStateReferencesTxn(txn, statePath, Strings(state_stateReferences.begin(), state_stateReferences.end()), 0, newTimestamp);
		
		printMsg(lvlError, format("Reverted state of '%1%' to revision '%2%'") % statePath % revision_arg);
	}
}



//TODO maybe add user ID ?
Snapshots commitStatePathTxn(const Transaction & txn, const Path & statePath)
{
	if(!isValidStatePathTxn(txn, statePath))
		throw Error(format("path `%1%' is not a valid state path") % statePath);
    
	//queryDeriversStatePath??
	Derivation drv = derivationFromPathTxn(txn, queryStatePathDrvTxn(txn, statePath));
  	DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    DerivationStateOutputDirs stateOutputDirs = drv.stateOutputDirs;

	printMsg(lvlError, format("Snapshotting statePath: %1%") % statePath);

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
	IntVector intervals = getStatePathsIntervalTxn(txn, intervalPaths);
	
	Snapshots revisions_list;
	
	int intervalAt=0;	
	for (DerivationStateOutputDirs::const_iterator i = stateOutputDirs.begin(); i != stateOutputDirs.end(); ++i){
		DerivationStateOutputDir d = i->second;
		string thisdir = d.path;
		
		string fullstatedir = statePath + "/" + thisdir;
		if(thisdir == "/")									//exception for the root dir
			fullstatedir = statePath + "/";				
		
		if(d.type == "none"){
			continue;
		}
		
		if(d.type == "interval"){
			//Get the interval-counter from the database
			int interval_counter = intervals[intervalAt];
			int interval = d.getInterval();

			//update the interval
			intervals[intervalAt] = interval_counter + 1;
			intervalAt++;
						
			if(interval_counter % interval != 0){	continue;		}
		}
		else if(d.type == "full"){	}			
		else if(d.type == "manual"){ 	continue; 	}		//TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!			
		else
			throw Error(format("Type '%1%' is not handled in nix-state") % d.type);
			
		//We got here so we need to commit
		unsigned int revision_number;
		if(pathExists(fullstatedir) || FileExist(fullstatedir)){
			revision_number = take_snapshot(fullstatedir);
			printMsg(lvlError, format("Snapshotted '%1%@%2%'") % fullstatedir % revision_number);
		}
		else
			revision_number = 0;	//deleted, so we assign 0 to indicate that 	
		
		revisions_list[fullstatedir] = revision_number;
	}
	  	
	return revisions_list; 
	 
    //Update the intervals again	
	//setStatePathsIntervalTxn(txn, intervalPaths, intervals);		//TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!! uncomment
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
    queryAllValidPathsTxn(txn, allComponentPaths, allStatePaths);
    
    //Remove derivation paths
	PathSet allComponentPaths2;	//without derivations
    for (PathSet::iterator i = allComponentPaths.begin(); i != allComponentPaths.end(); ++i){
    	string path = *i;
    	if(path.substr(path.length() - 4,path.length()) != drvExtension)		//TODO HACK: we should have a typed table or a seperate table ....	 	drvExtension == ".drv"
    		allComponentPaths2.insert(path);
    }

	//TODO maybe only scan in the changeset (patch) for new references? (this will be difficult and depending on the underlying versioning system)

    //Scan in for (new) component and state references
    PathSet state_references = scanForReferences(statePath, allComponentPaths2);
    PathSet state_stateReferences = scanForStateReferences(statePath, allStatePaths);
    
    //Retrieve old references
    PathSet old_references;
    PathSet old_state_references;
    queryXReferencesTxn(txn, statePath, old_references, true, 0);				//get the latsest references
    queryXReferencesTxn(txn, statePath, old_state_references, false, 0);		
    
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

	//We always set the referernces so we know they were scanned (maybe the same) at a certain time
   	printMsg(lvlError, format("Updating new references for statepath: '%1%'") % statePath);
   	Path drvPath = queryStatePathDrvTxn(txn, statePath);
   	registerValidPath(txn,    	
    		statePath,
    		Hash(),				//emtpy hash
    		state_references,
    		state_stateReferences,
    		drvPath,
    		0);				//Set at a new timestamp
}

void scanAndUpdateAllReferencesRecusivelyTxn(const Transaction & txn, const Path & statePath)
{
   	if(! isValidStatePathTxn(txn, statePath))
   		throw Error(format("This path '%1%' is not a state path") % statePath);

	//get all state current state references recursively
	PathSet statePaths;
	storePathRequisitesTxn(txn, statePath, false, statePaths, false, true, 0);		//Get all current state dependencies
	
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

}
