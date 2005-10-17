#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <string>
#include <set>
#include "util.hh"

using namespace std;

/* Path names. */

/* nixStore is the directory where we generally store atomic and
   derived files. */
extern string nixStore;

extern string nixDataDir; /* !!! fix */

/* nixLogDir is the directory where we log various operations. */ 
extern string nixLogDir;

/* nixStateDir is the directory where state is stored. */
extern string nixStateDir;

/* nixDBPath is the path name of our Berkeley DB environment. */
extern string nixDBPath;

/* nixConfDir is the directory where configuration files are
   stored. */
extern string nixConfDir;



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


Strings querySetting(const string & name, const Strings & def);

bool queryBoolSetting(const string & name, bool def);


#endif /* !__GLOBALS_H */
