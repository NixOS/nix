#ifndef __VALUES_H
#define __VALUES_H

#include <string>

#include "hash.hh"

using namespace std;


void copyFile(string src, string dst);

/* Copy a file to the nixStore directory and register it in dbRefs.
   Return the hash code of the value. */
void addToStore(string srcPath, string & dstPath, Hash & hash);

/* Delete a value from the nixStore directory. */
void deleteFromStore(Hash hash);

/* !!! */
string queryFromStore(Hash hash);


#endif /* !__VALUES_H */
