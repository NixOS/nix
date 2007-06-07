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

namespace nix {


void updatedStateDerivation(Path storePath)
{
	//Remove the old .svn folders
		
	//Create new repositorys, or use existing...
	//createStateDirs already does that ...
}



void createStateDirs(const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs, const StringPairs & env)
{
	Path statePath = stateOutputs.find("state")->second.statepath;
	string stateDir = statePath;
	string drvName = env.find("name")->second;
	string stateIdentifier = stateOutputs.find("state")->second.stateIdentifier;
	
	string svnbin = nixSVNPath + "/svn";
	string svnadminbin = nixSVNPath + "/svnadmin";
	
	PathSet intervalPaths;
	
	//TODO check if we can create stata and staterepos dirs
	
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		DerivationStateOutputDir d = i->second;

		string thisdir = d.path;
		string fullstatedir = stateDir + "/" + thisdir;
		Path statePath = fullstatedir;					//TODO call coerce function
				
		//TODO REPLACE TRUE INTO VAR OF CREATEING DIRS BEFORE OR AFTER INSTALL
		//Check if and how this dir needs to be versioned
		if(d.type == "none"){
			if(true){
				executeAndPrintShellCommand("mkdir -p " + fullstatedir, "mkdir");
			}
			continue;
		}
		
		//Create a repository for this state location
		string repos = makeStateReposPath("stateOutput:staterepospath", stateDir, thisdir, drvName, stateIdentifier);
		executeAndPrintShellCommand("mkdir -p " + repos, "mkdir");
		executeAndPrintShellCommand(svnadminbin + " create " + repos, "svnadmin");				 //TODO create as nixbld.nixbld chmod 700... can you still commit than ??

		if(d.type == "interval"){
			intervalPaths.insert(statePath);
		}

		//TODO REPLACE TRUE INTO VAR OF CREATEING DIRS BEFORE OR AFTER INSTALL
		if(true){
			printMsg(lvlError, format("Adding state subdir: %1% to %2% from repository %3%") % thisdir % fullstatedir % repos);
			string checkoutcommand = svnbin + " checkout file://" + repos + " " + fullstatedir;
			executeAndPrintShellCommand(checkoutcommand, "svn");  //TODO checkout as user	
		}
	}
	
	//Initialize the counters for the statePaths that have an interval to 0
	vector<int> empty;
	store->setStatePathsInterval(intervalPaths, empty, true);
}


}
