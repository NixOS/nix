#ifndef __STORE_H
#define __STORE_H

#include <string>

#include "hash.hh"
#include "db.hh"

using namespace std;


/* Copy a path recursively. */
void copyPath(const Path & src, const Path & dst);

/* Register a substitute. */
void registerSubstitute(const Path & srcPath, const Path & subPath);

/* Register the validity of a path. */
void registerValidPath(const Transaction & txn, const Path & path);

/* Unregister the validity of a path. */
void unregisterValidPath(const Path & path);

/* Checks whether a path is valid. */ 
bool isValidPath(const Path & path);

/* Copy the contents of a path to the store and register the validity
   the resulting path.  The resulting path is returned. */
Path addToStore(const Path & srcPath);

/* Delete a value from the nixStore directory. */
void deleteFromStore(const Path & path);

void verifyStore();


#endif /* !__STORE_H */
