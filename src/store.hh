#ifndef __STORE_H
#define __STORE_H

#include <string>

#include "hash.hh"

using namespace std;


typedef Hash FSId;


/* Copy a path recursively. */
void copyPath(string src, string dst);

/* Register a substitute. */
void registerSubstitute(const FSId & srcId, const FSId & subId);

/* Register a path keyed on its id. */
void registerPath(const string & path, const FSId & id);

/* Query the id of a path. */
bool queryPathId(const string & path, FSId & id);

/* Return a path whose contents have the given hash.  If target is
   not empty, ensure that such a path is realised in target (if
   necessary by copying from another location).  If prefix is not
   empty, only return a path that is an descendent of prefix. */
string expandId(const FSId & id, const string & target = "",
    const string & prefix = "/");

/* Copy a file to the nixStore directory and register it in dbRefs.
   Return the hash code of the value. */
void addToStore(string srcPath, string & dstPath, FSId & id,
    bool deterministicName = false);

/* Delete a value from the nixStore directory. */
void deleteFromStore(const string & path);

void verifyStore();


#endif /* !__STORE_H */
