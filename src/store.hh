#ifndef __VALUES_H
#define __VALUES_H

#include <string>

#include "hash.hh"

using namespace std;


void copyPath(string src, string dst);

/* Register a path keyed on its hash. */
Hash registerPath(const string & path, Hash hash = Hash());

/* Query a path (any path) through its hash. */
string queryPathByHash(Hash hash);

/* Copy a file to the nixStore directory and register it in dbRefs.
   Return the hash code of the value. */
void addToStore(string srcPath, string & dstPath, Hash & hash);

/* Delete a value from the nixStore directory. */
void deleteFromStore(const string & path);


#endif /* !__VALUES_H */
