#include "globals.hh"

string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixStateDir = "/UNINIT";
string nixDBPath = "/UNINIT";

bool keepFailed = false;

bool keepGoing = false;

bool tryFallback = false;

Verbosity buildVerbosity = lvlInfo;

unsigned int maxBuildJobs = 1;

bool readOnlyMode = false;
