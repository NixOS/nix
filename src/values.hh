#ifndef __VALUES_H
#define __VALUES_H

#include <string>

#include "hash.hh"

using namespace std;


/* Copy a value to the nixValues directory and register it in dbRefs.
   Return the hash code of the value. */
Hash addValue(string pathName);


/* Obtain the path of a value with the given hash.  If a file with
   that hash is known to exist in the local file system (as indicated
   by the dbRefs database), we use that.  Otherwise, we attempt to
   fetch it from the network (using dbNetSources).  We verify that the
   file has the right hash. */
string queryValuePath(Hash hash);


#endif /* !__VALUES_H */
