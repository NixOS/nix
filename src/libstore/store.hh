#ifndef __STORE_H
#define __STORE_H

#include <string>

#include "hash.hh"
#include "db.hh"

using namespace std;


/* A substitute is a program invocation that constructs some store
   path (typically by fetching it from somewhere, e.g., from the
   network). */
struct Substitute
{
    /* Store expression to be normalised and realised in order to
       obtain `program'. */
    Path storeExpr;

    /* Program to be executed to create the store path.  Must be in
       the output path of `storeExpr'. */
    Path program;

    /* Extra arguments to be passed to the program (the first argument
       is the store path to be substituted). */
    Strings args;

    bool operator == (const Substitute & sub);
};

typedef list<Substitute> Substitutes;


/* Open the database environment. */
void openDB();

/* Create the required database tables. */
void initDB();

/* Get a transaction object. */
void createStoreTransaction(Transaction & txn);

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

/* Remove a successor mapping. */
void unregisterSuccessor(const Path & srcPath);

/* Return the predecessors of the Nix expression stored at the given
   path. */
bool querySuccessor(const Path & srcPath, Path & sucPath);

/* Return the predecessors of the Nix expression stored at the given
   path. */
Paths queryPredecessors(const Path & sucPath);

/* Register a substitute. */
void registerSubstitute(const Transaction & txn,
    const Path & srcPath, const Substitute & sub);

/* Return the substitutes expression for the given path. */
Substitutes querySubstitutes(const Path & srcPath);

/* Register the validity of a path. */
void registerValidPath(const Transaction & txn, const Path & path);

/* Throw an exception if `path' is not directly in the Nix store. */
void assertStorePath(const Path & path);

/* Checks whether a path is valid. */ 
bool isValidPath(const Path & path);

/* Copy the contents of a path to the store and register the validity
   the resulting path.  The resulting path is returned. */
Path addToStore(const Path & srcPath);

/* Like addToStore, but the path of the output is given, and the
   contents written to the output path is a regular file containing
   the given string. */
void addTextToStore(const Path & dstPath, const string & s);

/* Delete a value from the nixStore directory. */
void deleteFromStore(const Path & path);

void verifyStore();


#endif /* !__STORE_H */
