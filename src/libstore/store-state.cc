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
	//nothing to do since New repostorys are created by createStateDirs
		
	printMsg(lvlTalkative, format("Resetting state drv settings like repositorys"));
	 
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

	//check if we can create state and staterepos dirs
	//TODO
	
	//Create a repository for this state location
	string repos = getStateReposPath("stateOutput:staterepospath", stateDir, drvName, stateIdentifier);

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


}
