#ifndef __STORE_H
#define __STORE_H

#include <string>

#include "hash.hh"
#include "db.hh"

using namespace std;


const int nixSchemaVersion = 2;


/* Path hashes are the hash components of store paths, e.g., the
   `zvhgns772jpj68l40mq1jb74wpfsf0ma' in
   `/nix/store/zvhgns772jpj68l40mq1jb74wpfsf0ma-glibc'.  These are
   truncated SHA-256 hashes of the path contents,  */
struct PathHash
{
private:
    string rep;
public:
    PathHash();
    PathHash(const Hash & h);
    PathHash(const string & h);
    string toString() const;
    bool PathHash::isNull() const;
    bool operator ==(const PathHash & hash2) const;
    bool operator <(const PathHash & hash2) const;
};



#if 0
/* A substitute is a program invocation that constructs some store
   path (typically by fetching it from somewhere, e.g., from the
   network). */
struct Substitute
{       
    /* The derivation that built this store path (empty if none). */
    Path deriver;
    
    /* Program to be executed to create the store path.  Must be in
       the output path of `storeExpr'. */
    Path program;

    /* Extra arguments to be passed to the program (the first argument
       is the store path to be substituted). */
    Strings args;

    bool operator == (const Substitute & sub) const;
};

typedef list<Substitute> Substitutes;
#endif


/* A trust identifier, which is a name of an entity involved in a
   trust relation.  Right now this is just a user ID (e.g.,
   `root'). */
typedef string TrustId;


/* An output path equivalence class.  They represent outputs of
   derivations.  That is, a derivation can have several outputs (e.g.,
   `out', `lib', `man', etc.), each of which maps to a output path
   equivalence class.  They can map to a number of concrete paths,
   depending on what users built the derivation.

   Equivalence classes are actually "placeholder" store paths that
   never get built.  They do occur in derivations however in
   command-line arguments and environment variables, but get
   substituted with concrete paths when we actually build. */
typedef Path OutputEqClass;


/* A member of an output path equivalence class, i.e., a store path
   that has been produced by a certain derivation. */
struct OutputEqMember
{
    TrustId trustId;
    Path path;
};

typedef list<OutputEqMember> OutputEqMembers;


/* Open the database environment. */
void openDB();

/* Create the required database tables. */
void initDB();

/* Get a transaction object. */
void createStoreTransaction(Transaction & txn);


/* Copy a path recursively. */
void copyPath(const Path & src, const Path & dst);


#if 0
/* Register a substitute. */
void registerSubstitute(const Transaction & txn,
    const Path & srcPath, const Substitute & sub);

/* Return the substitutes for the given path. */
Substitutes querySubstitutes(const Transaction & txn, const Path & srcPath);

/* Deregister all substitutes. */
void clearSubstitutes();
#endif


/* Register the validity of a path, i.e., that `path' exists, that the
   paths referenced by it exists, and in the case of an output path of
   a derivation, that it has been produced by a succesful execution of
   the derivation (or something equivalent).  Also register the hash
   of the file system contents of the path.  The hash must be a
   SHA-256 hash. */
void registerValidPath(const Transaction & txn,
    const Path & path, const Hash & hash, const PathSet & references,
    const Path & deriver);

struct ValidPathInfo 
{
    Path path;
    Path deriver;
    Hash hash;
    PathSet references;
};

typedef list<ValidPathInfo> ValidPathInfos;

void registerValidPaths(const Transaction & txn,
    const ValidPathInfos & infos);


/* Throw an exception if `path' is not directly in the Nix store. */
void assertStorePath(const Path & path);

bool isInStore(const Path & path);
bool isStorePath(const Path & path);

void checkStoreName(const string & name);

/* Chop off the parts after the top-level store name, e.g.,
   /nix/store/abcd-foo/bar => /nix/store/abcd-foo. */
Path toStorePath(const Path & path);

PathHash hashPartOf(const Path & path);

string namePartOf(const Path & path);


/* "Fix", or canonicalise, the meta-data of the files in a store path
   after it has been built.  In particular:
   - the last modification date on each file is set to 0 (i.e.,
     00:00:00 1/1/1970 UTC)
   - the permissions are set of 444 or 555 (i.e., read-only with or
     without execute permission; setuid bits etc. are cleared)
   - the owner and group are set to the Nix user and group, if we're
     in a setuid Nix installation. */
void canonicalisePathMetaData(const Path & path);

/* Checks whether a path is valid. */ 
bool isValidPathTxn(const Transaction & txn, const Path & path);
bool isValidPath(const Path & path);


/* Queries the hash of a valid path. */ 
Hash queryPathHash(const Path & path);

/* Sets the set of outgoing FS references for a store path.  Use with
   care! */
void setReferences(const Transaction & txn, const Path & storePath,
    const PathSet & references);

/* Queries the set of outgoing FS references for a store path.  The
   result is not cleared. */
void queryReferences(const Transaction & txn,
    const Path & storePath, PathSet & references);

/* Queries the set of incoming FS references for a store path.  The
   result is not cleared. */
void queryReferers(const Transaction & txn,
    const Path & storePath, PathSet & referers);

void addOutputEqMember(const Transaction & txn,
    const OutputEqClass & eqClass, const TrustId & trustId,
    const Path & path);

void queryOutputEqMembers(const Transaction & txn,
    const OutputEqClass & eqClass, OutputEqMembers & members);
    
#if 0
/* Sets the deriver of a store path.  Use with care! */
void setDeriver(const Transaction & txn, const Path & storePath,
    const Path & deriver);

/* Query the deriver of a store path.  Return the empty string if no
   deriver has been set. */
Path queryDeriver(const Transaction & txn, const Path & storePath);
#endif


/* Constructs a unique store path name. */
void makeStorePath(const Hash & contentHash, const string & suffix,
    Path & path, PathHash & pathHash);

/* Constructs a random store path name.  Only to be used for temporary
   build outputs, since these will violate the hash invariant. */
Path makeRandomStorePath(const string & suffix);


/* Hash rewriting. */
typedef map<PathHash, PathHash> HashRewrites;

string rewriteHashes(string s, const HashRewrites & rewrites,
    vector<int> & positions);

string rewriteHashes(const string & s, const HashRewrites & rewrites);


/* Copy the contents of a path to the store and register the validity
   the resulting path.  The resulting path is returned. */
Path addToStore(const Path & srcPath, const PathHash & selfHash = PathHash(),
    const string & suffix = "", const PathSet & references = PathSet());

#if 0
/* Like addToStore(), but for pre-adding the outputs of fixed-output
   derivations. */
Path addToStoreFixed(bool recursive, string hashAlgo, const Path & srcPath);

Path makeFixedOutputPath(bool recursive,
    string hashAlgo, Hash hash, string name);
#endif

/* Like addToStore, but the contents written to the output path is a
   regular file containing the given string. */
Path addTextToStore(const string & suffix, const string & s,
    const PathSet & references);


/* Delete a value from the nixStore directory. */
void deleteFromStore(const Path & path);

void verifyStore(bool checkContents);


#endif /* !__STORE_H */
