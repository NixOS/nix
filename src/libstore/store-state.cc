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
	//update in database?
		
	printMsg(lvlTalkative, format("Resetting state drv settings"));
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
		
		//If it is a file: continue
		if(thisdir.substr(thisdir.length() -1 , thisdir.length()) != "/")
			continue;
		
		Path fullstatedir = stateDir + "/" + thisdir;
		
		ensureDirExists(fullstatedir);
		
		if(d.type == "interval"){
			intervalPaths.insert(fullstatedir);
		}		
	}
	
	//Initialize the counters for the statePaths that have an interval to 0
	vector<int> empty;
	store->setStatePathsInterval(intervalPaths, empty, true);
}

Snapshots commitStatePathTxn(const Transaction & txn, const Path & statePath)
{
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

}
