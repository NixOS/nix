#include "db.hh"
#include "hash.hh"
#include "util.hh"
#include "local-store.hh"
#include "globals.hh"
#include "pathlocks.hh"
#include "config.h"

#include <iostream>


namespace nix {


Hash parseHashField(const Path & path, const string & s);


/* Upgrade from schema 4 (Nix 0.11) to schema 5 (Nix >= 0.12).  The
   old schema uses Berkeley DB, the new one stores store path
   meta-information in files. */
void LocalStore::upgradeStore12()
{
#if OLD_DB_COMPAT
    
#ifdef __CYGWIN__
    /* Cygwin can't upgrade a read lock to a write lock... */
    lockFile(globalLock, ltNone, true);
#endif

    if (!lockFile(globalLock, ltWrite, false)) {
        printMsg(lvlError, "waiting for exclusive access to the Nix store...");
        lockFile(globalLock, ltWrite, true);
    }

    printMsg(lvlError, "upgrading Nix store to new schema (this may take a while)...");

    if (getSchema() >= nixSchemaVersion) return; /* somebody else beat us to it */

    /* Open the old Nix database and tables. */
    Database nixDB;
    nixDB.open(nixDBPath);
    
    /* dbValidPaths :: Path -> ()

       The existence of a key $p$ indicates that path $p$ is valid
       (that is, produced by a succesful build). */
    TableId dbValidPaths = nixDB.openTable("validpaths");

    /* dbReferences :: Path -> [Path]

       This table lists the outgoing file system references for each
       output path that has been built by a Nix derivation.  These are
       found by scanning the path for the hash components of input
       paths. */
    TableId dbReferences = nixDB.openTable("references");

    /* dbReferrers :: Path -> Path

       This table is just the reverse mapping of dbReferences.  This
       table can have duplicate keys, each corresponding value
       denoting a single referrer. */
    // Not needed for conversion: it's just the inverse of
    // references.
    // TableId dbReferrers = nixDB.openTable("referrers");

    /* dbDerivers :: Path -> [Path]

       This table lists the derivation used to build a path.  There
       can only be multiple such paths for fixed-output derivations
       (i.e., derivations specifying an expected hash). */
    TableId dbDerivers = nixDB.openTable("derivers");

    Paths paths;
    nixDB.enumTable(noTxn, dbValidPaths, paths);
    
    foreach (Paths::iterator, i, paths) {
        ValidPathInfo info;
        info.path = *i;
        
        Paths references;
        nixDB.queryStrings(noTxn, dbReferences, *i, references);
        info.references.insert(references.begin(), references.end());
        
        string s;
        nixDB.queryString(noTxn, dbValidPaths, *i, s);
        info.hash = parseHashField(*i, s);
        
        nixDB.queryString(noTxn, dbDerivers, *i, info.deriver);
        
        registerValidPath(info, true);
        std::cerr << ".";
    }

    std::cerr << std::endl;

    writeFile(schemaPath, (format("%1%") % nixSchemaVersion).str());

    lockFile(globalLock, ltRead, true);

#else
    throw Error(
        "Your Nix store has a database in Berkeley DB format. To convert\n"
        "to the new format, please compile Nix with Berkeley DB support.");
#endif
}


}
