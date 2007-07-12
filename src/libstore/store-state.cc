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
	
	string svnbin = nixSVNPath + "/svn";
	string svnadminbin = nixSVNPath + "/svnadmin";
	
	PathSet intervalPaths;

	//check if we can create state and staterepos dirs
	//TODO
	
	//Create a repository for this state location
	string repos = getStateReposPath("stateOutput:staterepospath", stateDir);

	printMsg(lvlTalkative, format("Adding statedir '%1%' from repository '%2%'") % stateDir % repos);
	
	if(IsDirectory(repos))
		printMsg(lvlTalkative, format("Repos %1% already exists, so we use that repository") % repos);			
	else{
		Strings p_args;
		p_args.push_back("create");
		p_args.push_back(repos);
		runProgram_AndPrintOutput(svnadminbin, true, p_args, "svnadmin"); 				 //TODO create as nixbld.nixbld chmod 700... can you still commit then ??
	}
	
	string statedir_svn = stateDir + "/.svn/";
	if( ! IsDirectory(statedir_svn) ){
		Strings p_args;
		p_args.push_back("checkout");
		p_args.push_back("file://" + repos);
		p_args.push_back(stateDir);
		runProgram_AndPrintOutput(svnbin, true, p_args, "svn");	//TODO checkout as user
	}
	else
		printMsg(lvlTalkative, format("Statedir %1% already exists, so dont check out its repository again") % statedir_svn);	
	
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
	if(!isValidStatePathTxn(txn, statePath))
		throw Error(format("path `%1%' is not a valid state path") % statePath);
    
	
	//queryDeriversStatePath??
	Derivation drv = derivationFromPath(queryStatePathDrvTxn(txn, statePath));
	
	//Extract the neccecary info from each Drv
  	DerivationStateOutputs stateOutputs = drv.stateOutputs; 
   	//Path statePath = stateOutputs.find("state")->second.statepath;
    DerivationStateOutputDirs stateOutputDirs = drv.stateOutputDirs;

	//Specifiy the SVN binarys
    string svnbin = nixSVNPath + "/svn";
	string svnadminbin = nixSVNPath + "/svnadmin";

	//Print
	printMsg(lvlError, format("Committing statePath: %1%") % statePath);

	//Vector includeing all commit scripts:
	vector<string> subversionedpaths;
	vector<bool> subversionedpathsCommitBoolean;
	vector<string> nonversionedpaths;			//of type none, no versioning needed
	
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
	vector<int> intervals = store->getStatePathsInterval(intervalPaths);		//TODO !!!!!!!!!!!!! txn ??
	
	int intervalAt=0;	
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		DerivationStateOutputDir d = i->second;
		string thisdir = d.path;
		
		string fullstatedir = statePath + "/" + thisdir;
		if(thisdir == "/")									//exception for the root dir
			fullstatedir = statePath + "/";				
		
		if(d.type == "none"){
			nonversionedpaths.push_back(fullstatedir);
			continue;
		}
				
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
	
	//Get the a repository for this state location
	string repos = getStateReposPath("stateOutput:staterepospath", statePath);		//this is a copy from store-state.cc
	string checkoutcommand = svnbin + " --ignore-externals checkout file://" + repos + " " + statePath;
			
	//Call the commit script with the appropiate paramenters
	string subversionedstatepathsarray; 
	for (vector<string>::iterator i = subversionedpaths.begin(); i != subversionedpaths.end(); ++i)
		subversionedstatepathsarray += *(i) + " ";

	string subversionedpathsCommitBooleansarray; 
	for (vector<bool>::iterator i = subversionedpathsCommitBoolean.begin(); i != subversionedpathsCommitBoolean.end(); ++i)
		subversionedpathsCommitBooleansarray += bool2string(*i) + " ";

	string nonversionedstatepathsarray; 
	for (vector<string>::iterator i = nonversionedpaths.begin(); i != nonversionedpaths.end(); ++i)
		nonversionedstatepathsarray += *(i) + " ";
	  	
    //make the call to the commit script
    Strings p_args;
	p_args.push_back(svnbin);
	p_args.push_back(subversionedstatepathsarray);
	p_args.push_back(subversionedpathsCommitBooleansarray);
	p_args.push_back(nonversionedstatepathsarray);
	p_args.push_back(checkoutcommand);
	p_args.push_back(statePath);
	runProgram_AndPrintOutput(nixLibexecDir + "/nix/nix-statecommit.sh", true, p_args, "svn");
    
    //Update the intervals again	
	//setStatePathsIntervalTxn(txn, intervalPaths, intervals);		//TODO!!!!!!!!!!!!!!!!!!!!!! uncomment and txn ??
}

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


}
