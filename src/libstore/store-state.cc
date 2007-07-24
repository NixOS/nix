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

namespace nix {


void updatedStateDerivation(Path storePath)
{
	//We dont remove the old .svn folders
	//nothing to do since New repostorys are created by createStateDirs
		
	printMsg(lvlTalkative, format("Resetting state drv settings like repositorys"));
	 
}

void createStateDirs(const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs)
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
		
		//Check if it is a file
		if(thisdir.substr(thisdir.length() -1 , thisdir.length()) != "/")
			continue;
		
		Path fullstatedir = stateDir + "/" + thisdir;
		
		Strings p_args;
		p_args.push_back("-p");
		p_args.push_back(fullstatedir);
		runProgram_AndPrintOutput("mkdir", true, p_args, "mkdir");
		
		if(d.type == "interval"){
			intervalPaths.insert(fullstatedir);
		}		
	}
	
	//Initialize the counters for the statePaths that have an interval to 0
	vector<int> empty;
	store->setStatePathsInterval(intervalPaths, empty, true);
}

void commitStatePathTxn(const Transaction & txn, const Path & statePath)
{
	//TODO: include code from:	updateRevisionsRecursivelyTxn(txn, root_statePath);
	
	if(!isValidStatePathTxn(txn, statePath))
		throw Error(format("path `%1%' is not a valid state path") % statePath);
    
	//queryDeriversStatePath??
	Derivation drv = derivationFromPath(queryStatePathDrvTxn(txn, statePath));
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
	vector<int> intervals = store->getStatePathsInterval(intervalPaths);		//TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! txn
	
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
						
			if(interval_counter % interval != 0){	return;		}
		}
		else if(d.type == "full"){	}			
		else if(d.type == "manual"){ return; }		//TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!			
		else
			throw Error(format("Type '%1%' is not handled in nix-state") % d.type);
			
		//We got here so we need to commit
		
		unsigned int epoch_time;
		
		if(pathExists(fullstatedir) || FileExist(fullstatedir)){
			epoch_time = take_snapshot(fullstatedir);
			printMsg(lvlError, format("Snapshotted '%1%' with id '%2%'") % fullstatedir % epoch_time);
		}
		else
		{
			//TODO !!!!!!!!!!!!!!	
		}
		
		//Put in database !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		//TODO
	}
	  	
    //Update the intervals again	
	//setStatePathsIntervalTxn(txn, intervalPaths, intervals);		//TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!! uncomment
}

/*
 * This function takes all state requisites (references) and revision numbers and stores them ...
 */
/* 
void updateRevisionsRecursivelyTxn(const Transaction & txn, const Path & statePath)
{
	//Save all revisions for the call to 
	RevisionNumbersSet rivisionMapping;

	PathSet statePaths;
	storePathRequisitesTxn(txn, statePath, false, statePaths, false, true, -1);		//Get all current state dependencies
	
	//Add own statePath (may already be in there, but its a set, so no doubles)
	statePaths.insert(statePath);
	
   	//Sort
   	vector<Path> sortedStatePaths;
	for (PathSet::iterator i = statePaths.begin(); i != statePaths.end(); ++i)
		sortedStatePaths.push_back(*i);
    sort(sortedStatePaths.begin(), sortedStatePaths.end());
		
	//call scanForAllReferences again on all newly found statePaths
	for (vector<Path>::const_iterator i = sortedStatePaths.begin(); i != sortedStatePaths.end(); ++i)
		rivisionMapping[*i] = readRevisionNumber(*i);	
	
	//Store the revision numbers in the database for this statePath with revision number
	setStateRevisionsTxn(txn, statePath, rivisionMapping);
} 

int readRevisionNumber(Path statePath)
{
	string svnbin = nixSVNPath + "/svn";
	RevisionNumbers revisions;
	
	string repos = getStateReposPath("stateOutput:staterepospath", statePath);		//this is a copy from store-state.cc
	
	//TODO Check if the .svn exists, it might be deleted, then we dont have to remember the state revision (set -1)
	
	Strings p_args;
	p_args.push_back(svnbin);
	p_args.push_back("file://" + repos);
	string output = runProgram(nixLibexecDir + "/nix/nix-readrevisions.sh", true, p_args);	//run
	    	
	int pos = output.find("\n",0);	//remove trailing \n
	output.erase(pos,1);
			
	int revision;
	bool succeed = string2Int(output, revision);
	if(!succeed)
		throw Error(format("Cannot read revision number of path '%1%'") % repos);				
	
	return revision;
}
*/

//	string s = "/media/ext3cow/cca/";
//	unsigned int i = take_snapshot(s);
//	printMsg(lvlError, format("SS: '%1%'") % i);


}
