#include "globals.hh"

string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixStateDir = "/UNINIT";
string nixDBPath = "/UNINIT";

bool keepFailed = false;

Verbosity buildVerbosity = lvlDebug;

unsigned int maxBuildJobs = 1;
