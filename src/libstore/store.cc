#include <iostream>
#include <algorithm>

#include <sys/wait.h>
#include <unistd.h>

#include "store.hh"
#include "globals.hh"
#include "db.hh"
#include "archive.hh"
#include "pathlocks.hh"


/* Nix database. */
static Database nixDB;


/* Database tables. */

/* dbValidPaths :: Path -> ()

   The existence of a key $p$ indicates that path $p$ is valid (that
   is, produced by a succesful build). */
static TableId dbValidPaths;

/* dbSuccessors :: Path -> Path

   Each pair $(p_1, p_2)$ in this mapping records the fact that the
   Nix expression stored at path $p_1$ has a successor expression
   stored at path $p_2$.

   Note that a term $y$ is a successor of $x$ iff there exists a
   sequence of rewrite steps that rewrites $x$ into $y$.
*/
static TableId dbSuccessors;

/* dbSuccessorsRev :: Path -> [Path]

   The reverse mapping of dbSuccessors (i.e., it stores the
   predecessors of a Nix expression).
*/
static TableId dbSuccessorsRev;

/* dbSubstitutes :: Path -> [Path]

   Each pair $(p, [ps])$ tells Nix that it can realise any of the
   Nix expressions stored at paths $ps$ to produce a path $p$.

   The main purpose of this is for distributed caching of derivates.
   One system can compute a derivate and put it on a website (as a Nix
   archive), for instance, and then another system can register a
   substitute for that derivate.  The substitute in this case might be
   a Nix expression that fetches the Nix archive.
*/
static TableId dbSubstitutes;

/* dbSubstitutesRev :: Path -> [Path]

   The reverse mapping of dbSubstitutes.
*/
static TableId dbSubstitutesRev;


void openDB()
{
    nixDB.open(nixDBPath);
    dbValidPaths = nixDB.openTable("validpaths");
    dbSuccessors = nixDB.openTable("successors");
    dbSuccessorsRev = nixDB.openTable("successors-rev");
    dbSubstitutes = nixDB.openTable("substitutes");
    dbSubstitutesRev = nixDB.openTable("substitutes-rev");
}


void initDB()
{
}


void createStoreTransaction(Transaction & txn)
{
    Transaction txn2(nixDB);
    txn2.moveTo(txn);
}


/* Path copying. */

struct CopySink : DumpSink
{
    int fd;
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        writeFull(fd, data, len);
    }
};


struct CopySource : RestoreSource
{
    int fd;
    virtual void operator () (unsigned char * data, unsigned int len)
    {
        readFull(fd, data, len);
    }
};


void copyPath(const Path & src, const Path & dst)
{
    debug(format("copying `%1%' to `%2%'") % src % dst);

    /* Unfortunately C++ doesn't support coprocedures, so we have no
       nice way to chain CopySink and CopySource together.  Instead we
       fork off a child to run the sink.  (Fork-less platforms should
       use a thread). */

    /* Create a pipe. */
    int fds[2];
    if (pipe(fds) == -1) throw SysError("creating pipe");

    /* Fork. */
    pid_t pid;
    switch (pid = fork()) {

    case -1:
        throw SysError("unable to fork");

    case 0: /* child */
        try {
            close(fds[1]);
            CopySource source;
            source.fd = fds[0];
            restorePath(dst, source);
            _exit(0);
        }  catch (exception & e) {
            cerr << "error: " << e.what() << endl;
        }
        _exit(1);        
    }

    close(fds[0]);
    
    /* Parent. */

    CopySink sink;
    sink.fd = fds[1];
    dumpPath(src, sink);

    /* Wait for the child to finish. */
    int status;
    if (waitpid(pid, &status, 0) != pid)
        throw SysError("waiting for child");

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw Error("cannot copy file: child died");
}


static bool isValidPathTxn(const Path & path, const Transaction & txn)
{
    string s;
    return nixDB.queryString(txn, dbValidPaths, path, s);
}


bool isValidPath(const Path & path)
{
    return isValidPathTxn(path, noTxn);
}


static bool isUsablePathTxn(const Path & path, const Transaction & txn)
{
    if (isValidPathTxn(path, txn)) return true;
    Paths subs;
    nixDB.queryStrings(txn, dbSubstitutes, path, subs);
    return subs.size() > 0;
}


void registerSuccessor(const Transaction & txn,
    const Path & srcPath, const Path & sucPath)
{
    if (!isUsablePathTxn(sucPath, txn))	throw Error(
	format("path `%1%' cannot be a successor, since it is not usable")
	% sucPath);

    Path known;
    if (nixDB.queryString(txn, dbSuccessors, srcPath, known) &&
        known != sucPath)
    {
        throw Error(format(
            "the `impossible' happened: expression in path "
            "`%1%' appears to have multiple successors "
            "(known `%2%', new `%3%'")
            % srcPath % known % sucPath);
    }

    Paths revs;
    nixDB.queryStrings(txn, dbSuccessorsRev, sucPath, revs);
    if (find(revs.begin(), revs.end(), srcPath) == revs.end())
        revs.push_back(srcPath);

    nixDB.setString(txn, dbSuccessors, srcPath, sucPath);
    nixDB.setStrings(txn, dbSuccessorsRev, sucPath, revs);
}


bool querySuccessor(const Path & srcPath, Path & sucPath)
{
    return nixDB.queryString(noTxn, dbSuccessors, srcPath, sucPath);
}


Paths queryPredecessors(const Path & sucPath)
{
    Paths revs;
    nixDB.queryStrings(noTxn, dbSuccessorsRev, sucPath, revs);
    return revs;
}


void registerSubstitute(const Path & srcPath, const Path & subPath)
{
    if (!isValidPathTxn(subPath, noTxn)) throw Error(
	format("path `%1%' cannot be a substitute, since it is not valid")
	% subPath);

    Transaction txn(nixDB);

    Paths subs;
    nixDB.queryStrings(txn, dbSubstitutes, srcPath, subs);

    if (find(subs.begin(), subs.end(), subPath) != subs.end()) {
        /* Nothing to do if the substitute is already known. */
        txn.abort();
        return;
    }
    subs.push_front(subPath); /* new substitutes take precedence */

    Paths revs;
    nixDB.queryStrings(txn, dbSubstitutesRev, subPath, revs);
    if (find(revs.begin(), revs.end(), srcPath) == revs.end())
        revs.push_back(srcPath);
    
    nixDB.setStrings(txn, dbSubstitutes, srcPath, subs);
    nixDB.setStrings(txn, dbSubstitutesRev, subPath, revs);

    txn.commit();
}


Paths querySubstitutes(const Path & srcPath)
{
    Paths subPaths;
    nixDB.queryStrings(noTxn, dbSubstitutes, srcPath, subPaths);
    return subPaths;
}


void registerValidPath(const Transaction & txn, const Path & _path)
{
    Path path(canonPath(_path));
    debug(format("registering path `%1%'") % path);
    nixDB.setString(txn, dbValidPaths, path, "");
}


static void setOrClearStrings(Transaction & txn,
    TableId table, const string & key, const Strings & value)
{
    if (value.size() > 0)
        nixDB.setStrings(txn, table, key, value);
    else
        nixDB.delPair(txn, table, key);
}


static void invalidatePath(const Path & path, Transaction & txn)
{
    debug(format("unregistering path `%1%'") % path);

    nixDB.delPair(txn, dbValidPaths, path);

    /* Remove any successor mappings to this path (but not *from*
       it). */
    Paths revs;
    nixDB.queryStrings(txn, dbSuccessorsRev, path, revs);
    for (Paths::iterator i = revs.begin(); i != revs.end(); ++i)
        nixDB.delPair(txn, dbSuccessors, *i);
    nixDB.delPair(txn, dbSuccessorsRev, path);

    /* Remove any substitute mappings to this path. */
    revs.clear();
    nixDB.queryStrings(txn, dbSubstitutesRev, path, revs);
    for (Paths::iterator i = revs.begin(); i != revs.end(); ++i) {
        Paths subs;
        nixDB.queryStrings(txn, dbSubstitutes, *i, subs);
        if (find(subs.begin(), subs.end(), path) == subs.end())
            throw Error("integrity error in substitutes mapping");
        subs.remove(path);
        setOrClearStrings(txn, dbSubstitutes, *i, subs);

	/* If path *i now has no substitutes left, and is not valid,
	   then it too should be invalidated.  This is because it may
	   be a substitute or successor. */
	if (subs.size() == 0 && !isValidPathTxn(*i, txn))
	    invalidatePath(*i, txn);
    }
    nixDB.delPair(txn, dbSubstitutesRev, path);
}


static bool isInPrefix(const string & path, const string & _prefix)
{
    string prefix = canonPath(_prefix + "/");
    return string(path, 0, prefix.size()) == prefix;
}


Path addToStore(const Path & _srcPath)
{
    Path srcPath(absPath(_srcPath));
    debug(format("adding `%1%' to the store") % srcPath);

    Hash h = hashPath(srcPath);

    string baseName = baseNameOf(srcPath);
    Path dstPath = canonPath(nixStore + "/" + (string) h + "-" + baseName);

    if (!isValidPath(dstPath)) { 

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        PathSet lockPaths;
        lockPaths.insert(dstPath);
        PathLocks outputLock(lockPaths);

        if (!isValidPath(dstPath)) {
            copyPath(srcPath, dstPath);

            Transaction txn(nixDB);
            registerValidPath(txn, dstPath);
            txn.commit();
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


void addTextToStore(const Path & dstPath, const string & s)
{
    if (!isValidPath(dstPath)) {

        PathSet lockPaths;
        lockPaths.insert(dstPath);
        PathLocks outputLock(lockPaths);

        if (!isValidPath(dstPath)) {
            writeStringToFile(dstPath, s);

            Transaction txn(nixDB);
            registerValidPath(txn, dstPath);
            txn.commit();
        }

        outputLock.setDeletion(true);
    }
}


void deleteFromStore(const Path & _path)
{
    Path path(canonPath(_path));

    if (!isInPrefix(path, nixStore))
        throw Error(format("path `%1%' is not in the store") % path);

    Transaction txn(nixDB);
    invalidatePath(path, txn);
    txn.commit();

    deletePath(path);
}


void verifyStore()
{
    Transaction txn(nixDB);

    Paths paths;
    PathSet validPaths;
    nixDB.enumTable(txn, dbValidPaths, paths);

    for (Paths::iterator i = paths.begin(); i != paths.end(); ++i) {
        Path path = *i;
        if (!pathExists(path)) {
            printMsg(lvlError, format("path `%1%' disappeared") % path);
            invalidatePath(path, txn);
        } else
            validPaths.insert(path);
    }

    /* !!! the code below does not allow transitive substitutes.
       I.e., if B is a substitute of A, then B must be a valid path.
       B cannot itself be invalid but have a substitute. */

    /* "Usable" paths are those that are valid or have a substitute.
       These are the paths that are allowed to appear in the
       right-hand side of a sute mapping. */
    PathSet usablePaths(validPaths);

    /* Check that the values of the substitute mappings are valid
       paths. */ 
    Paths subs;
    nixDB.enumTable(txn, dbSubstitutes, subs);
    for (Paths::iterator i = subs.begin(); i != subs.end(); ++i) {
        Paths subPaths, subPaths2;
        nixDB.queryStrings(txn, dbSubstitutes, *i, subPaths);
        for (Paths::iterator j = subPaths.begin(); j != subPaths.end(); ++j)
            if (validPaths.find(*j) == validPaths.end())
                printMsg(lvlError,
                    format("found substitute mapping to non-existent path `%1%'") % *j);
            else
                subPaths2.push_back(*j);
        if (subPaths.size() != subPaths2.size())
            setOrClearStrings(txn, dbSubstitutes, *i, subPaths2);
	if (subPaths2.size() > 0)
	    usablePaths.insert(*i);
    }

    /* Check that the keys of the reverse substitute mappings are
       valid paths. */ 
    Paths rsubs;
    nixDB.enumTable(txn, dbSubstitutesRev, rsubs);
    for (Paths::iterator i = rsubs.begin(); i != rsubs.end(); ++i) {
        if (validPaths.find(*i) == validPaths.end()) {
            printMsg(lvlError,
                format("found reverse substitute mapping for non-existent path `%1%'") % *i);
            nixDB.delPair(txn, dbSubstitutesRev, *i);
        }
    }

    /* Check that the values of the successor mappings are usable
       paths. */ 
    Paths sucs;
    nixDB.enumTable(txn, dbSuccessors, sucs);
    for (Paths::iterator i = sucs.begin(); i != sucs.end(); ++i) {
        /* Note that *i itself does not have to be valid, just its
           successor. */
        Path sucPath;
        if (nixDB.queryString(txn, dbSuccessors, *i, sucPath) &&
            usablePaths.find(sucPath) == usablePaths.end())
        {
            printMsg(lvlError,
                format("found successor mapping to non-existent path `%1%'") % sucPath);
            nixDB.delPair(txn, dbSuccessors, *i);
        }
    }

    /* Check that the keys of the reverse successor mappings are valid
       paths. */ 
    Paths rsucs;
    nixDB.enumTable(txn, dbSuccessorsRev, rsucs);
    for (Paths::iterator i = rsucs.begin(); i != rsucs.end(); ++i) {
        if (usablePaths.find(*i) == usablePaths.end()) {
            printMsg(lvlError,
                format("found reverse successor mapping for non-existent path `%1%'") % *i);
            nixDB.delPair(txn, dbSuccessorsRev, *i);
        }
    }

    txn.commit();
}
