#ifndef __STORE_H
#define __STORE_H

#include <string>

#include "hash.hh"
#include "db.hh"

using namespace std;


typedef Hash FSId;

typedef set<FSId> FSIdSet;


/* Copy a path recursively. */
void copyPath(string src, string dst);

/* Register a substitute. */
void registerSubstitute(const FSId & srcId, const FSId & subId);

/* Register a path keyed on its id. */
void registerPath(const Transaction & txn,
    const string & path, const FSId & id);

/* Query the id of a path. */
bool queryPathId(const string & path, FSId & id);

/* Return a path whose contents have the given hash.  If target is
   not empty, ensure that such a path is realised in target (if
   necessary by copying from another location).  If prefix is not
   empty, only return a path that is an descendent of prefix.

   The list of pending ids are those that already being expanded.
   This prevents infinite recursion for ids realised through a
   substitute (since when we build the substitute, we would first try
   to expand the id... kaboom!). */
string expandId(const FSId & id, const string & target = "",
    const string & prefix = "/", FSIdSet pending = FSIdSet(),
    bool ignoreSubstitutes = false);

/* Copy a file to the nixStore directory and register it in dbRefs.
   Return the hash code of the value. */
void addToStore(string srcPath, string & dstPath, FSId & id,
    bool deterministicName = false);

/* Delete a value from the nixStore directory. */
void deleteFromStore(const string & path);

void verifyStore();


#endif /* !__STORE_H */
