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

/* nixStateDir is the directory where state is stored. */
extern string nixStateDir;

/* nixDBPath is the path name of our Berkeley DB environment. */
extern string nixDBPath;

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

/* Whether buildDerivations() should print out lines on stderr in a
   fixed format to allow its progress to be monitored.  Each line
   starts with a "@".  The following are defined:

   @ build-started <drvpath> <outpath> <system> <logfile>
   @ build-failed <drvpath> <outpath> <exitcode> <error text>
   @ build-succeeded <drvpath> <outpath>
   @ substituter-started <outpath> <substituter>
   @ substituter-failed <outpath> <exitcode> <error text>
   @ substituter-succeeded <outpath>

   Best combined with --no-build-output, otherwise stderr might
   conceivably contain lines in this format printed by the builders.
*/
extern bool printBuildTrace;


Strings querySetting(const string & name, const Strings & def);

string querySetting(const string & name, const string & def);

bool queryBoolSetting(const string & name, bool def);

unsigned int queryIntSetting(const string & name, unsigned int def);

void overrideSetting(const string & name, const Strings & value);

void reloadSettings();

    
}


#endif /* !__GLOBALS_H */
