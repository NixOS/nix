#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <string>

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


/* Misc. global flags. */

/* Whether to keep temporary directories of failed builds. */
extern bool keepFailed;


#endif /* !__GLOBALS_H */
