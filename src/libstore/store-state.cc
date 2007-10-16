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

//for nixDB

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
	if( ! DirectoryExists( ....... ) ){
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


	//If not recursive: filter out all paths execpt statePath_ns
	if(!recursive)	
	{
		Snapshots currentSS = getRivisions[statePath_ns];		//save SS of statePath_ns
		getRivisions.clear();									//clear all
		getRivisions[statePath_ns] = currentSS; 				//insert
		
		unsigned int currentTS = getTimestamps[statePath_ns];	//same as above	
		getTimestamps.clear();
		getTimestamps[statePath_ns] = currentTS;
	}
	

	//Revert each statePath in the list
	for (RevisionClosure::iterator i = getRivisions.begin(); i != getRivisions.end(); ++i){
		Path statePath = (*i).first;
		Snapshots revisioned_paths = (*i).second;
		unsigned int timestamp = getTimestamps[statePath];
		
		
		//get its derivation-state-items
		//Derivation statePath_drv = derivationFromPathTxn(txn, queryStatePathDrvTxn(txn, statePath));
		//DerivationStateOutputDirs stateOutputDirs = statePath_drv.stateOutputDirs; 
		
		//TODO Sort snapshots??? eg first restore root, then the subdirs??
		
		for (Snapshots::iterator j = revisioned_paths.begin(); j != revisioned_paths.end(); ++j){
			Path revertPathOrFile = (*j).first;
			unsigned int epoch = (*j).second;			//TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			
			printMsg(lvlError, format("MAYBE '%1%''%2%'") % revertPathOrFile % epoch);
			
			
			/*
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
			string cpcommand = "cp";		//TODO USE RSYNC!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			*/
			/*
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
			else{	//is file
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
			*/
		}
		
		
		//*** Now also revert state _references_ to the specific revision (the revision is already converted to a timestamp here)
		/*
		//Query the references of the old revision (already converted to a timestamp)		
    	PathSet state_references;
    	queryXReferencesTxn(txn, statePath, state_references, true, 0, timestamp);
		PathSet state_stateReferences;
	    queryXReferencesTxn(txn, statePath, state_stateReferences, false, 0, timestamp);
    	
    	//Now set these old references as the new references at the new (just created) Timestamp
    	setStateComponentReferencesTxn(txn, statePath, Strings(state_references.begin(), state_references.end()), 0, newTimestamp);
		setStateStateReferencesTxn(txn, statePath, Strings(state_stateReferences.begin(), state_stateReferences.end()), 0, newTimestamp);
		
		if(revision_arg == 0)
			printMsg(lvlError, format("Reverted state of '%1%' to the latest revision") % statePath);	 //TODO lookup the number
		else
			printMsg(lvlError, format("Reverted state of '%1%' to revision '%2%'") % statePath % revision_arg);
		*/
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
		//printMsg(lvlError, format("FSD %1% RN %2%") % fullstatedir % revision_number);
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
	//TODO Unshare the path:	
	//TODO Path statePath_ns = toNonSharedPathTxn(txn, statePath);

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

// ******************************************************* DB FUNCTIONS ********************************************************************

/* State specific db functions */

Path mergeToDBKey(const Path & statePath, const unsigned int intvalue)
{
	string prefix = "-KEY-";
	return statePath + prefix + unsignedInt2String(intvalue);
}

void splitDBKey(const Path & revisionedStatePath, Path & statePath, unsigned int & intvalue)
{
	string prefix = "-KEY-";
	
	int pos = revisionedStatePath.find_last_of(prefix);
	statePath = revisionedStatePath.substr(0, pos - prefix.length() + 1);
	//printMsg(lvlError, format("'%2%' - '%1%'") % revisionedStatePath.substr(pos+1, revisionedStatePath.length()) % int2String(pos));
	bool succeed = string2UnsignedInt(revisionedStatePath.substr(pos+1, revisionedStatePath.length()), intvalue);
	if(!succeed)
		throw Error(format("Malformed revision value of path '%1%'") % revisionedStatePath);
}

unsigned int getNewRevisionNumber(Database & nixDB, const Transaction & txn, TableId table,
   	const Path & statePath)
{
	//query
	string data;
	bool notEmpty = nixDB.queryString(txn, table, statePath, data);
	
	if(!notEmpty){
		nixDB.setString(txn, table, statePath, int2String(1));
		return 1;	//we begin counting from 1 since 0 is a special value representing the last revision	
	}
	
	unsigned int revision;
	bool succeed = string2UnsignedInt(data, revision);
	if(!succeed)
		throw Error(format("Malformed revision counter value of path '%1%'") % statePath);
	
	revision++;
	nixDB.setString(txn, table, statePath, unsignedInt2String(revision));
	
	return revision;
}

bool lookupHighestRevivison(const Strings & keys, const Path & statePath, string & key, unsigned int lowerthan)
{
	unsigned int highestRev = 0;

	//Lookup which key we need	
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i) {
		
		if((*i).substr(0, statePath.length()) != statePath || (*i).length() == statePath.length()) 		//dont check the new-revision key or other keys
			continue;

		//printMsg(lvlError, format("'%1%' - '%2%'") % *i % statePath);
		Path getStatePath;
		unsigned int getRevision;
		splitDBKey(*i, getStatePath, getRevision);
		if(getRevision > highestRev){
		
			if(lowerthan != 0){
				if(getRevision <= lowerthan)			//if we have an uppper limit, see to it that we downt go over it
					highestRev = getRevision;
			}
			else
				highestRev = getRevision;
		}
	}

	if(highestRev == 0)	//no records found
		return false;
	
	key = mergeToDBKey(statePath, highestRev);		//final key that matches revision + statePath
	return true;	
}

bool revisionToTimeStamp(Database & nixDB, const Transaction & txn, TableId revisions_table, const Path & statePath, const int revision, unsigned int & timestamp)
{
	string key = mergeToDBKey(statePath, revision);
	Strings references;
	bool notempty = nixDB.queryStrings(txn, revisions_table, key, references);
	
	if(notempty){	
		Path empty; 
		splitDBKey(*(references.begin()), empty, timestamp);	//extract the timestamp
		//printMsg(lvlError, format("PRINT '%1%'") % timestamp);
		return true;
	}
	else
		return false;		
}

void setStateReferences(Database & nixDB, const Transaction & txn, TableId references_table, TableId revisions_table,
   	const Path & statePath, const Strings & references, const unsigned int revision, const unsigned int timestamp)
{
	//printMsg(lvlError, format("setStateReferences '%1%' for '%2%'") % references_table % statePath);

	//Find the timestamp if we need	
	unsigned int timestamp2 = timestamp;
	if(revision == 0 && timestamp == 0)
		timestamp2 = getTimeStamp();
	else if(revision != 0 && timestamp == 0){
		bool found = revisionToTimeStamp(nixDB, txn, revisions_table, statePath, revision, timestamp2);
		if(!found)
			throw Error(format("Revision '%1%' cannot be matched to a timestamp...") % revision);
	}		

	//for (Strings::const_iterator i = references.begin(); i != references.end(); ++i)
	//	printMsg(lvlError, format("setStateReferences::: '%1%'") % *i);
	
	//Warning if it already exists
	Strings empty;
	if( nixDB.queryStrings(txn, references_table, mergeToDBKey(statePath, timestamp2), empty) )
		printMsg(lvlError, format("Warning: The timestamp '%1%' (now: '%5%')  / revision '%4%' already exists for set-references of path '%2%' with db '%3%'") 
		% timestamp2 % statePath % references_table % revision % getTimeStamp());
	
	//Create the key
	string key = mergeToDBKey(statePath, timestamp2);
		
	//printMsg(lvlError, format("Set references '%1%'") % key);
	//for (Strings::const_iterator i = references.begin(); i != references.end(); ++i)
	//	printMsg(lvlError, format("reference '%1%'") % *i);
	
	//Insert	
	nixDB.setStrings(txn, references_table, key, references, false);				//The false makes sure also empty references are set
}

bool queryStateReferences(Database & nixDB, const Transaction & txn, TableId references_table, TableId revisions_table,
   	const Path & statePath, Strings & references, const unsigned int revision, const unsigned int timestamp)
{
	//printMsg(lvlError, format("queryStateReferences '%1%' with revision '%2%'") % references_table % revision);

	//Convert revision to timestamp number useing the revisions_table
	unsigned int timestamp2 = timestamp; 
	if(timestamp == 0 && revision != 0){	
		bool found = revisionToTimeStamp(nixDB, txn, revisions_table, statePath, revision, timestamp2);
		if(!found)			//we are asked for references of some revision, but there are no references registered yet, so we return false;
			return false;
	}
	
	Strings keys;
	nixDB.enumTable(txn, references_table, keys);

	//Mabye we need the latest timestamp?
	string key = "";
	if(timestamp2 == 0){
		bool foundsomething = lookupHighestRevivison(keys, statePath, key);
		if(!foundsomething)
			return false;
		else
			return nixDB.queryStrings(txn, references_table, key, references);
	}
	
	//If a specific key is given: check if this timestamp exists key in the table
	key = mergeToDBKey(statePath, timestamp2);
	bool found = false;
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i) {
		if(key == *i){
			found = true;
			key = mergeToDBKey(statePath, timestamp2);
		}
	}
	
	//If it doesn't exist in the table then find the highest key lower than it
	if(!found){
		bool foundsomething = lookupHighestRevivison(keys, statePath, key, 0);
		if(!foundsomething)
			return false;
		//printMsg(lvlError, format("Warning: References for timestamp '%1%' not was not found, so taking the highest rev-key possible for statePath '%2%'") % timestamp2 % statePath);
	}
		
	return nixDB.queryStrings(txn, references_table, key, references);		//now that we have the key, we can query the references
}

void invalidateAllStateReferences(Database & nixDB, const Transaction & txn, TableId references_table, const Path & statePath)
{
	//Remove all references
	Strings keys;
	nixDB.enumTable(txn, references_table, keys);
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i){
		if((*i).substr(0, statePath.length()) == statePath)
			nixDB.delPair(txn, references_table, *i);
	}
}

void removeAllStatePathRevisions(Database & nixDB, const Transaction & txn, TableId revisions_table, 
	TableId revisions_comments, TableId snapshots_table, TableId statecounters, const Path & statePath)
{
	//Remove all revisions
	nixDB.delPair(txn, revisions_table, statePath);
	RevisionInfos revisions;
	store->queryAvailableStateRevisions(statePath, revisions);
	for (RevisionInfos::iterator i = revisions.begin(); i != revisions.end(); ++i){
		unsigned int rev = (*i).first;
		nixDB.delPair(txn, revisions_table, mergeToDBKey(statePath, rev));
		
		//Remove all comments
		nixDB.delPair(txn, revisions_comments, mergeToDBKey(statePath, rev));
	}

	//Remove all snapshots
	Strings keys;
	nixDB.enumTable(txn, snapshots_table, keys);
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i){
		if((*i).substr(0, statePath.length()) == statePath)
			nixDB.delPair(txn, snapshots_table, *i);
	}
	
	//Remove all state-counters
	keys.clear();
	nixDB.enumTable(txn, statecounters, keys);
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i){
		if((*i).substr(0, statePath.length()) == statePath)
			nixDB.delPair(txn, statecounters, *i);
	}	
}

void setStateRevisions(Database & nixDB, const Transaction & txn, TableId revisions_table, TableId revisions_comments,
	 TableId snapshots_table, const RevisionClosure & revisions, const Path & rootStatePath, const string & comment)
{
	if( !isStatePath(rootStatePath)	)	//weak check on statePath
		throw Error(format("StatePath '%1%' is not a statepath") % rootStatePath);
			
	unsigned int timestamp = getTimeStamp();
	
	//Insert all ss_epochs into snapshots_table with the current ts.
	for (RevisionClosure::const_iterator i = revisions.begin(); i != revisions.end(); ++i){
		string key = mergeToDBKey((*i).first, timestamp);
		Strings data;
		//the map<> takes care of the sorting on the Path
		for (Snapshots::const_iterator j = (*i).second.begin(); j != (*i).second.end(); ++j)
			data.push_back(mergeToDBKey((*j).first, (*j).second));
		nixDB.setStrings(txn, snapshots_table, key, data);
	}
	
	//Insert for each statePath a new revision record linked to the ss_epochs
	for (RevisionClosure::const_iterator i = revisions.begin(); i != revisions.end(); ++i){
		Path statePath = (*i).first;
		
		unsigned int revision = getNewRevisionNumber(nixDB, txn, revisions_table, statePath);	//get a new revision number
		
		string key = mergeToDBKey(statePath, revision);
		
		//get all its requisites
		PathSet statePath_references;
		storePathRequisitesTxn(txn, statePath, false, statePath_references, false, true, 0);
		statePath_references.insert(statePath);
		
		//save in db
		Strings data;
		for (PathSet::const_iterator j = statePath_references.begin(); j != statePath_references.end(); ++j)
			data.push_back(mergeToDBKey(*j, timestamp));		
		
		nixDB.setStrings(txn, revisions_table, key, data, false);				//The false makes sure also empty revisions are set
		
		//save the date and comments
		Strings metadata;
		metadata.push_back(unsignedInt2String(timestamp));
		
		//get all paths that point to the same state (using shareing) and check if one of them equals the rootStatePath
		PathSet sharedWith = getSharedWithPathSetRecTxn(txn, statePath);
		if(statePath == rootStatePath || sharedWith.find(rootStatePath) != sharedWith.end())
			metadata.push_back(comment);
		else
			metadata.push_back("Part of the snashot closure for " + rootStatePath);
		nixDB.setStrings(txn, revisions_comments, key, metadata);
		
	}
}   

bool queryStateRevisions(Database & nixDB, const Transaction & txn, TableId revisions_table, TableId snapshots_table,
   	const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, const unsigned int root_revision)
{
	string key;
	
	if(root_revision == 0){
		Strings keys;
		nixDB.enumTable(txn, revisions_table, keys);		//get all revisions
		bool foundsomething = lookupHighestRevivison(keys, statePath, key);
		if(!foundsomething)
			return false;
	}
	else
		key = mergeToDBKey(statePath, root_revision);
	
	//Get references pointing to snapshots_table from revisions_table with root_revision
	Strings statePaths;
	bool notempty = nixDB.queryStrings(txn, revisions_table, key, statePaths);
	
	if(!notempty)
		throw Error(format("Root revision '%1%' not found of statePath '%2%'") % unsignedInt2String(root_revision) % statePath);
		
	//For each statePath add the revisions
	for (Strings::iterator i = statePaths.begin(); i != statePaths.end(); ++i){
		
		Path getStatePath;
		unsigned int getTimestamp;
		splitDBKey(*i, getStatePath, getTimestamp);
		
		//query state versioined directorys/files
		/*
		vector<Path> sortedPaths;
		Derivation drv = derivationFromPathTxn(txn, queryStatePathDrvTxn(txn, getStatePath));
  		DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    	DerivationStateOutputDirs stateOutputDirs = drv.stateOutputDirs;
    	for (DerivationStateOutputDirs::const_iterator j = stateOutputDirs.begin(); j != stateOutputDirs.end(); ++j){
			string thisdir = (j->second).path;
			string fullstatedir = getStatePath + "/" + thisdir;
			if(thisdir == "/")									//exception for the root dir
				fullstatedir = statePath + "/";
			sortedPaths.push_back(fullstatedir);
    	}			
		sort(sortedPaths.begin(), sortedPaths.end());	//sort
		*/
		
		Strings snapshots_s;
		Snapshots snapshots;		
		nixDB.queryStrings(txn, snapshots_table, *i, snapshots_s);
		int counter=0;
		for (Strings::iterator j = snapshots_s.begin(); j != snapshots_s.end(); ++j){
			
			Path snapshottedPath;			
			unsigned int revision;
			splitDBKey(*j, snapshottedPath, revision);
						
			snapshots[snapshottedPath] = revision;
			counter++;
		}
		
		revisions[getStatePath] = snapshots;
		timestamps[getStatePath] = getTimestamp;
	}
	
	return notempty;
}  

//TODO include comments into revisions?
bool queryAvailableStateRevisions(Database & nixDB, const Transaction & txn, TableId revisions_table, TableId revisions_comments,
    const Path & statePath, RevisionInfos & revisions)
{
	Strings keys;
	nixDB.enumTable(txn, revisions_table, keys);		//get all revisions
	
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i) {
	
		if((*i).substr(0, statePath.length()) != statePath || (*i).length() == statePath.length()) 		//dont check the new-revision key or other keys
			continue;

		Path getStatePath;
		unsigned int getRevision;
		splitDBKey(*i, getStatePath, getRevision);
		
		//save the date and comments
		RevisionInfo rev;
		Strings metadata;
		nixDB.queryStrings(txn, revisions_comments, *i, metadata);
		unsigned int ts;
		bool succeed = string2UnsignedInt(*(metadata.begin()), ts);
		if(!succeed)
			throw Error(format("Malformed timestamp in the revisions table of path '%1%'") % *i);
		rev.timestamp = ts;
		metadata.pop_front();		
		rev.comment = *(metadata.begin());
		revisions[getRevision] = rev; 
	}
    
    if(revisions.empty())
    	return false;
    else
    	return true;
}

void rsyncPaths(const Path & from, const Path & to)	//TODO bool shellexpansion, bool failure for nix-env
{
	//TODO Could be a symlink (to a non-existing dir)
	/*
	if(!DirectoryExist(from))
		throw Error(format("Path `%1%' doenst exist ...") % from);
	if(!DirectoryExist(to))
		throw Error(format("Path `%1%' doenst exist ...") % to);
	*/
	
	//We add a slash / to the end to ensure the contents is copyed
	Path from2 = from;
	Path to2 = to;
	if(from2[from2.length() - 1] != '/')
		from2 = from2 + "/";
	if(to2[to2.length() - 1] != '/')
		to2 = to2 + "/";
	
	printMsg(lvlError, format("Rsync from: '%1%' to: '%2%'") % from2 % to2);
	
	//Rsync from --> to and also with '-avHx --delete'
	//This makes the paths completely equal (also deletes) and retains times ownership etc.
	Strings p_args;
	p_args.push_back("-avHx");
	p_args.push_back("--delete");
	p_args.push_back(from2);
	p_args.push_back(to2);
	runProgram_AndPrintOutput(nixRsync, true, p_args, "rsync");
}

}
