#ifndef __STORE_H
#define __STORE_H

#include <string>

#include "hash.hh"

using namespace std;


/* Copy a path recursively. */
void copyPath(string src, string dst);

/* Register a substitute. */
void registerSubstitute(const Hash & srcHash, const Hash & subHash);

/* Register a path keyed on its hash. */
Hash registerPath(const string & path, Hash hash = Hash());

/* Return a path whose contents have the given hash.  If target is
   not empty, ensure that such a path is realised in target (if
   necessary by copying from another location).  If prefix is not
   empty, only return a path that is an descendent of prefix. 

   If no path with the given hash is known to exist in the file
   system, 
*/
string expandHash(const Hash & hash, const string & target = "",
    const string & prefix = "/");

/* Copy a file to the nixStore directory and register it in dbRefs.
   Return the hash code of the value. */
void addToStore(string srcPath, string & dstPath, Hash & hash,
    bool deterministicName = false);

/* Delete a value from the nixStore directory. */
void deleteFromStore(const string & path);


#endif /* !__STORE_H */
