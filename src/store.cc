#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>

#include "store.hh"
#include "globals.hh"
#include "db.hh"
#include "archive.hh"
#include "pathlocks.hh"


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
    const Path & path1, const Path & path2)
{
    Path known;
    if (nixDB.queryString(txn, dbSuccessors, path1, known) &&
        known != path2)
    {
        throw Error(format(
            "the `impossible' happened: expression in path "
            "`%1%' appears to have multiple successors "
            "(known `%2%', new `%3%'")
            % path1 % known % path2);
    }

    Paths revs;
    nixDB.queryStrings(txn, dbSuccessorsRev, path2, revs);
    revs.push_back(path1);

    nixDB.setString(txn, dbSuccessors, path1, path2);
    nixDB.setStrings(txn, dbSuccessorsRev, path2, revs);
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
    revs.push_back(srcPath);
    
    nixDB.setStrings(txn, dbSubstitutes, srcPath, subs);
    nixDB.setStrings(txn, dbSubstitutesRev, subPath, revs);

    txn.commit();
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

    /* !!! verify that the result is consistent */

#if 0
    Strings paths;
    nixDB.enumTable(txn, dbPath2Id, paths);

    for (Strings::iterator i = paths.begin();
         i != paths.end(); i++)
    {
        bool erase = true;
        string path = *i;

        if (!pathExists(path)) {
            debug(format("path `%1%' disappeared") % path);
        }

        else {
            string id;
            if (!nixDB.queryString(txn, dbPath2Id, path, id)) abort();

            Strings idPaths;
            nixDB.queryStrings(txn, dbId2Paths, id, idPaths);

            bool found = false;
            for (Strings::iterator j = idPaths.begin();     
                 j != idPaths.end(); j++)
                if (path == *j) {
                    found = true;
                    break;
                }

            if (found)
                erase = false;
            else
                /* !!! perhaps we should add path to idPaths? */
                debug(format("reverse mapping for path `%1%' missing") % path);
        }

        if (erase) nixDB.delPair(txn, dbPath2Id, path);
    }

    Strings ids;
    nixDB.enumTable(txn, dbId2Paths, ids);

    for (Strings::iterator i = ids.begin();
         i != ids.end(); i++)
    {
        FSId id = parseHash(*i);

        Strings idPaths;
        nixDB.queryStrings(txn, dbId2Paths, id, idPaths);

        for (Strings::iterator j = idPaths.begin();     
             j != idPaths.end(); )
        {
            string id2;
            if (!nixDB.queryString(txn, dbPath2Id, *j, id2) || 
                id != parseHash(id2)) {
                debug(format("erasing path `%1%' from mapping for id %2%") 
                    % *j % (string) id);
                j = idPaths.erase(j);
            } else j++;
        }

        nixDB.setStrings(txn, dbId2Paths, id, idPaths);
    }

    
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

    Strings sucs;
    nixDB.enumTable(txn, dbSuccessors, sucs);

    for (Strings::iterator i = sucs.begin();
         i != sucs.end(); i++)
    {
        FSId id1 = parseHash(*i);

        string id2;
        if (!nixDB.queryString(txn, dbSuccessors, id1, id2)) abort();
        
        Strings id2Paths;
        nixDB.queryStrings(txn, dbId2Paths, id2, id2Paths);
        if (id2Paths.size() == 0) {
            Strings id2Subs;
            nixDB.queryStrings(txn, dbSubstitutes, id2, id2Subs);
            if (id2Subs.size() == 0) {
                debug(format("successor %1% for %2% missing") 
                    % id2 % (string) id1);
                nixDB.delPair(txn, dbSuccessors, (string) id1);
            }
        }
    }
#endif

    txn.commit();
}
