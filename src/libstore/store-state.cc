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
	//We dont remove the old .svn folders
	//New repostorys are created by createStateDirs
		
	printMsg(lvlTalkative, format("Resetting state drv settings like repositorys"));
	
	//Create a repository for this state location
	
	//
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

	//Make sure the 'root' path which holds the repositorys exists, so svn doenst complain.
	string repos_root_path = getStateReposRootPath("stateOutput:staterepospath", stateDir, drvName, stateIdentifier);
	executeAndPrintShellCommand("mkdir -p " + repos_root_path, "mkdir");
	
	//TODO check if we can create state and staterepos dirs
	
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		DerivationStateOutputDir d = i->second;

		string thisdir = d.path;
		string fullstatedir = stateDir + "/" + thisdir;
		Path statePath = fullstatedir;					//TODO call coerce function
				
		//Check if and how this dir needs to be versioned
		if(d.type == "none"){
			executeAndPrintShellCommand("mkdir -p " + fullstatedir, "mkdir");
			continue;
		}
		
		//Create a repository for this state location
		string repos = getStateReposPath("stateOutput:staterepospath", stateDir, thisdir, drvName, stateIdentifier);
		
		
		if(IsDirectory(repos))
			printMsg(lvlTalkative, format("Repos %1% already exists, so we use that repository") % repos);			
		else
			executeAndPrintShellCommand(svnadminbin + " create " + repos, "svnadmin");				 //TODO create as nixbld.nixbld chmod 700... can you still commit then ??

		if(d.type == "interval"){
			intervalPaths.insert(statePath);
		}

		printMsg(lvlTalkative, format("Adding state subdir: %1% to %2% from repository %3%") % thisdir % fullstatedir % repos);
			
		string fullstatedir_svn = fullstatedir + "/.svn/";
		if( ! IsDirectory(fullstatedir_svn) ){
			string checkoutcommand = svnbin + " checkout file://" + repos + " " + fullstatedir;
			executeAndPrintShellCommand(checkoutcommand, "svn");  //TODO checkout as user
		}
		else
			printMsg(lvlTalkative, format("Statedir %1% already exists, so dont check out its repository again") % fullstatedir_svn);
	}
	
	//Initialize the counters for the statePaths that have an interval to 0
	vector<int> empty;
	store->setStatePathsInterval(intervalPaths, empty, true);
}



}
