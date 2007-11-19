#ifndef __GLOBALS_H
#define __GLOBALS_H

#include "types.hh"


namespace nix {


/* Path names. */

/* nixStore is the directory where we generally store atomic and
   derived files. */
extern string nixStore;

extern string nixDataDir; /* !!! fix */

/* nixLogDir is the directory where we log various operations. */ 
extern string nixLogDir;

/* nixStoreState is the directory where the state dirs of the components are stored.  */
extern string nixStoreState;

/* nixStateDir is the directory where state is stored. */
extern string nixStateDir;

/* nixDBPath is the path name of our Berkeley DB environment. */
extern string nixDBPath;

/* nixExt3CowHeader is the header file used to communicate with ext3cow. */
extern string nixExt3CowHeader;

/* nixRsync is used to copy from one statedir to the other. */
extern string nixRsync;

/* nixConfDir is the directory where configuration files are
   stored. */
extern string nixConfDir;

/* nixLibexecDir is the directory where internal helper programs are
   stored. */
extern string nixLibexecDir;

/* nixBinDir is the directory where the main programs are stored. */
extern string nixBinDir;

/* Misc. global flags. */

/* Whether to keep temporary directories of failed builds. */
extern bool keepFailed;

/* Whether to keep building subgoals when a sibling (another subgoal
   of the same goal) fails. */
extern bool keepGoing;

/* Whether, if we cannot realise the known closure corresponding to a
   derivation, we should try to normalise the derivation instead. */
extern bool tryFallback;

/* Verbosity level for build output. */
extern Verbosity buildVerbosity;

/* Maximum number of parallel build jobs.  0 means unlimited. */
extern unsigned int maxBuildJobs;

/* Read-only mode.  Don't copy stuff to the store, don't change the
   database. */
extern bool readOnlyMode;

/* The canonical system name, as returned by config.guess. */ 
extern string thisSystem;

/* The maximum time in seconds that a builer can go without producing
   any output on stdout/stderr before it is killed.  0 means
   infinity. */
extern unsigned int maxSilentTime;

/* The substituters.  There are programs that can somehow realise a
   store path without building, e.g., by downloading it or copying it
   from a CD. */
extern Paths substituters;

/* Whether to use build hooks (for distributed builds).  Sometimes
   users want to disable this from the command-line. */
extern bool useBuildHook;

Strings querySetting(const string & name, const Strings & def);

string querySetting(const string & name, const string & def);

bool queryBoolSetting(const string & name, bool def);

unsigned int queryIntSetting(const string & name, unsigned int def);

/* TODO PRIVATE: UID of the user that calls the nix-worker daemon */
extern uid_t callingUID;

/* get/set the UID of the user that calls the nix-worker daemon */
uid_t queryCallingUID();
void setCallingUID(uid_t uid, bool reset = false);

/* Convert a uid to a username: Watch it! this segfaults when given a wrong uid !! */
string uidToUsername(uid_t uid);

/* get the username based on the UID of the user that calls the nix-worker daemon */
string queryCallingUsername();

/* get the username based on the UID of the user currently runs the process */
string queryCurrentUsername();

/* Debugging variables */
extern bool singleThreaded;
extern bool sendOutput;
extern bool sleepForGDB;    
  
}


#endif /* !__GLOBALS_H */
