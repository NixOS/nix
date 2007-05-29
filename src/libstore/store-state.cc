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

void createStateDirs(const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs, const StringPairs & env)
{
	Path statePath = stateOutputs.find("state")->second.statepath;
	string stateDir = statePath;
	string drvName = env.find("name")->second;
	
	//Convert the map into a sortable vector
	vector<DerivationStateOutputDir> stateDirsVector;	
	for (DerivationStateOutputDirs::const_reverse_iterator i = stateOutputDirs.rbegin(); i != stateOutputDirs.rend(); ++i){
		stateDirsVector.push_back(i->second);
	}
	sort(stateDirsVector.begin(), stateDirsVector.end());
	
	printMsg(lvlError, format("stateDir `%1%'") % stateDir);

	string svnbin = nixSVNPath + "/svn";
	string svnadminbin = nixSVNPath + "/svnadmin";
				
	//Vector includeing all commit scripts:
	vector<string> commitscript;
	vector<string> subversionedpaths;
	vector<int> subversionedpathsInterval;
	vector<string> nonversionedpaths;			//of type none, no versioning needed
	vector<string> checkoutcommands;
	PathSet statePaths;
	
	for (vector<DerivationStateOutputDir>::iterator i = stateDirsVector.begin(); i != stateDirsVector.end(); ++i)
    {
		DerivationStateOutputDir d = *(i);

		string thisdir = d.path;
		string fullstatedir = stateDir + "/" + thisdir;
		Path statePath = fullstatedir;					//TODO call coerce function
				
		//calc create repos for this state location
		Hash hash = hashString(htSHA256, stateDir + thisdir);
		string repos = makeStateReposPath("stateOutput:staterepospath", hash, drvName); 
		
		//Were going to execute svn shell commands
		executeAndPrintShellCommand(svnadminbin + " create " + repos, "svnadmin");				  //TODO create as nixbld.nixbld chmod 700

		//TODO REPLACE TRUE INTO VAR
	
		//Check if and how this dir needs to be versioned
		if(d.type == "none"){
			if(true){
				executeAndPrintShellCommand("mkdir -p " + fullstatedir, "mkdir");
			}
			nonversionedpaths.push_back(fullstatedir);
			continue;
		}

		string checkoutcommand = svnbin + " checkout file://" + repos + " " + fullstatedir;
		checkoutcommands.push_back(checkoutcommand);
		subversionedpaths.push_back(fullstatedir);
		
		if(d.type == "interval"){
			statePaths.insert(statePath);
			subversionedpathsInterval.push_back(d.getInterval());
		}
		else
			subversionedpathsInterval.push_back(0);

		if(true){
			printMsg(lvlError, format("Adding state subdir: %1% to %2% from repository %3%") % thisdir % fullstatedir % repos);
			executeAndPrintShellCommand(checkoutcommand, "svn");  //TODO checkout as user	
		}
	}
	
	//Add the statePaths that have an interval
	vector<int> empty;
	store->setStatePathsInterval(statePaths, empty, true);
	
	//create super commit script
	printMsg(lvlError, format("svnbin=%1%") % svnbin);
	string subversionedstatepathsarray = "subversionedpaths=( "; 
	for (vector<string>::iterator i = subversionedpaths.begin(); i != subversionedpaths.end(); ++i)
    {
		subversionedstatepathsarray += *(i) + " ";
    }
    printMsg(lvlError, format("%1%)") % subversionedstatepathsarray);
	string subversionedpathsIntervalsarray = "subversionedpathsInterval=( "; 
	for (vector<int>::iterator i = subversionedpathsInterval.begin(); i != subversionedpathsInterval.end(); ++i)
    {
		subversionedpathsIntervalsarray += int2String(*i) + " ";
    }
	printMsg(lvlError, format("%1%)") % subversionedpathsIntervalsarray);
	string nonversionedstatepathsarray = "nonversionedpaths=( "; 
	for (vector<string>::iterator i = nonversionedpaths.begin(); i != nonversionedpaths.end(); ++i)
    {
		nonversionedstatepathsarray += *(i) + " ";
    }
	printMsg(lvlError, format("%1%)") % nonversionedstatepathsarray);
	string commandsarray = "checkouts=( "; 
	for (vector<string>::iterator i = checkoutcommands.begin(); i != checkoutcommands.end(); ++i)
    {
		commandsarray += "\"" + *(i) + "\" ";
    }
	printMsg(lvlError, format("%1%)") % commandsarray);
	for (vector<string>::iterator i = commitscript.begin(); i != commitscript.end(); ++i)
    {
    	string s = *(i);
    	printMsg(lvlError, format("%1%") % s);
    }    	
}

void executeAndPrintShellCommand(const string & command, const string & commandName)
{
	string tempoutput = "svnoutput.txt";
	string newcommand = command + " > " + tempoutput;

	int kidstatus, deadpid;
	pid_t kidpid = fork();
    switch (kidpid) {
	    case -1:
	        throw SysError("unable to fork");
	    case 0:
	        try { // child
	            int rv = system(newcommand.c_str());
				//int rv = execlp(svnbin.c_str(), svnbin.c_str(), ">", tempoutput.c_str(), NULL);		//TODO make this work ... ?
				
				string line;
				std::ifstream myfile (tempoutput.c_str());
				if (myfile.is_open()){
					while (! myfile.eof() )
				    {
				      getline (myfile,line);
				      if(trim(line) != "")
					      printMsg(lvlError, format("[%2%]: %1%") % line % commandName);
				    }
				    myfile.close();
				}
				else{
					throw SysError("svn state error");
	            	quickExit(1);
				} 
	            
				if (rv == -1) {
					throw SysError("svn state error");
					quickExit(99);
				}
		        quickExit(0);
				
	        } catch (std::exception & e) {
	            std::cerr << format("state child error: %1%\n") % e.what();
	            quickExit(1);
	        }
    }
    deadpid = waitpid(kidpid, &kidstatus, 0);
    if (deadpid == -1) {
        std::cerr << format("state child waitpid error\n");
        quickExit(1);
    }
    
    remove(tempoutput.c_str());	//Remove the tempoutput file
}

}
