#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
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


void registerSuccessor(const Transaction & txn,
    const Path & srcPath, const Path & sucPath)
{
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


bool isValidPath(const Path & path)
{
    string s;
    return nixDB.queryString(noTxn, dbValidPaths, path, s);
}


void unregisterValidPath(const Path & _path)
{
    Path path(canonPath(_path));
    Transaction txn(nixDB);

    debug(format("unregistering path `%1%'") % path);

    nixDB.delPair(txn, dbValidPaths, path);

    txn.commit();
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
    }

    return dstPath;
}


void addTextToStore(const Path & dstPath, const string & s)
{
    if (!isValidPath(dstPath)) {

        /* !!! locking? -> parallel writes are probably idempotent */

        AutoCloseFD fd = open(dstPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (fd == -1) throw SysError(format("creating store file `%1%'") % dstPath);

        if (write(fd, s.c_str(), s.size()) != (ssize_t) s.size())
            throw SysError(format("writing store file `%1%'") % dstPath);

        Transaction txn(nixDB);
        registerValidPath(txn, dstPath);
        txn.commit();
    }
}


void deleteFromStore(const Path & _path)
{
    Path path(canonPath(_path));

    if (!isInPrefix(path, nixStore))
        throw Error(format("path `%1%' is not in the store") % path);

    unregisterValidPath(path);

    deletePath(path);
}


void verifyStore()
{
    Transaction txn(nixDB);

    Paths paths;
    nixDB.enumTable(txn, dbValidPaths, paths);

    for (Paths::iterator i = paths.begin();
         i != paths.end(); i++)
    {
        Path path = *i;
        if (!pathExists(path)) {
            debug(format("path `%1%' disappeared") % path);
            nixDB.delPair(txn, dbValidPaths, path);
            nixDB.delPair(txn, dbSuccessorsRev, path);
            nixDB.delPair(txn, dbSubstitutesRev, path);
        }
    }

#if 0    
    Strings subs;
    nixDB.enumTable(txn, dbSubstitutes, subs);

    for (Strings::iterator i = subs.begin();
         i != subs.end(); i++)
    {
        FSId srcId = parseHash(*i);

        Strings subIds;
        nixDB.queryStrings(txn, dbSubstitutes, srcId, subIds);

        for (Strings::iterator j = subIds.begin();     
             j != subIds.end(); )
        {
            FSId subId = parseHash(*j);
            
            Strings subPaths;
            nixDB.queryStrings(txn, dbId2Paths, subId, subPaths);
            if (subPaths.size() == 0) {
                debug(format("erasing substitute %1% for %2%") 
                    % (string) subId % (string) srcId);
                j = subIds.erase(j);
            } else j++;
        }

        nixDB.setStrings(txn, dbSubstitutes, srcId, subIds);
    }
#endif

    Paths sucs;
    nixDB.enumTable(txn, dbSuccessors, sucs);

    for (Paths::iterator i = sucs.begin(); i != sucs.end(); i++) {
        Path srcPath = *i;

        Path sucPath;
        if (!nixDB.queryString(txn, dbSuccessors, srcPath, sucPath)) abort();

        Paths revs;
        nixDB.queryStrings(txn, dbSuccessorsRev, sucPath, revs);

        if (find(revs.begin(), revs.end(), srcPath) == revs.end()) {
            debug(format("reverse successor mapping from `%1%' to `%2%' missing")
                  % srcPath % sucPath);
            revs.push_back(srcPath);
            nixDB.setStrings(txn, dbSuccessorsRev, sucPath, revs);
        }
    }

    txn.commit();
}
