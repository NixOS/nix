#ifndef __STORE_H
#define __STORE_H

#include <string>

#include "hash.hh"
#include "db.hh"

using namespace std;


/* Copy a path recursively. */
void copyPath(const Path & src, const Path & dst);

/* Register a successor.  This function accepts a transaction handle
   so that it can be enclosed in an atomic operation with calls to
   registerValidPath().  This must be atomic, since if we register a
   successor for a derivation without registering the paths built in
   the derivation, we have a successor with dangling pointers, and if
   we do it in reverse order, we can get an obstructed build (since to
   rebuild the successor, the outputs paths must not exist). */
void registerSuccessor(const Transaction & txn,
    const Path & srcPath, const Path & sucPath);

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
