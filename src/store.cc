#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>

#include "store.hh"
#include "globals.hh"
#include "db.hh"
#include "archive.hh"
#include "normalise.hh"


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


void copyPath(string src, string dst)
{
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


void registerSubstitute(const FSId & srcId, const FSId & subId)
{
#if 0
    Strings subs;
    queryListDB(nixDB, dbSubstitutes, srcId, subs); /* non-existence = ok */

    for (Strings::iterator it = subs.begin(); it != subs.end(); it++)
        if (parseHash(*it) == subId) return;
    
    subs.push_back(subId);
    
    setListDB(nixDB, dbSubstitutes, srcId, subs);
#endif

    /* For now, accept only one substitute per id. */
    Strings subs;
    subs.push_back(subId);
    setListDB(nixDB, dbSubstitutes, srcId, subs);
}


void registerPath(const string & _path, const FSId & id)
{
    string path(canonPath(_path));

    setDB(nixDB, dbPath2Id, path, id);

    Strings paths;
    queryListDB(nixDB, dbId2Paths, id, paths); /* non-existence = ok */

    for (Strings::iterator it = paths.begin();
         it != paths.end(); it++)
        if (*it == path) return;
    
    paths.push_back(path);
    
    setListDB(nixDB, dbId2Paths, id, paths);
}


void unregisterPath(const string & _path)
{
    string path(canonPath(_path));

    string _id;
    if (!queryDB(nixDB, dbPath2Id, path, _id))
        return;
    FSId id(parseHash(_id));

    delDB(nixDB, dbPath2Id, path);

    /* begin transaction */
    
    Strings paths, paths2;
    queryListDB(nixDB, dbId2Paths, id, paths); /* non-existence = ok */

    bool changed = false;
    for (Strings::iterator it = paths.begin();
         it != paths.end(); it++)
        if (*it != path) paths2.push_back(*it); else changed = true;

    if (changed)
        setListDB(nixDB, dbId2Paths, id, paths2);

    /* end transaction */

}


bool queryPathId(const string & path, FSId & id)
{
    string s;
    if (!queryDB(nixDB, dbPath2Id, absPath(path), s)) return false;
    id = parseHash(s);
    return true;
}


bool isInPrefix(const string & path, const string & _prefix)
{
    string prefix = canonPath(_prefix + "/");
    return string(path, 0, prefix.size()) == prefix;
}


string expandId(const FSId & id, const string & target,
    const string & prefix)
{
    Strings paths;

    if (!target.empty() && !isInPrefix(target, prefix))
        abort();

    queryListDB(nixDB, dbId2Paths, id, paths);

    /* Pick one equal to `target'. */
    if (!target.empty()) {

        for (Strings::iterator i = paths.begin();
             i != paths.end(); i++)
        {
            string path = *i;
            if (path == target && pathExists(path))
                return path;
        }
        
    }

    /* Arbitrarily pick the first one that exists and isn't stale. */
    for (Strings::iterator it = paths.begin();
         it != paths.end(); it++)
    {
        string path = *it;
        if (isInPrefix(path, prefix) && pathExists(path)) {
            if (target.empty())
                return path;
            else {
                copyPath(path, target);
                registerPath(target, id);
                return target;
            }
        }
    }

    /* Try to realise the substitutes. */

    Strings subs;
    queryListDB(nixDB, dbSubstitutes, id, subs); /* non-existence = ok */

    for (Strings::iterator it = subs.begin(); it != subs.end(); it++) {
        FSId subId = parseHash(*it);
        Slice slice = normaliseFState(subId);
        realiseSlice(slice);
        
        Strings paths = fstatePaths(subId, true);
        if (paths.size() != 1) 
            throw Error("substitute created more than 1 path");
        string path = *(paths.begin());

        if (target.empty())
            return path; /* !!! prefix */
        else {
            if (path != target) {
                copyPath(path, target);
                registerPath(target, id);
            }
            return target;
        }
    }
    
    throw Error(format("cannot expand id `%1%'") % (string) id);
}

    
void addToStore(string srcPath, string & dstPath, FSId & id,
    bool deterministicName)
{
    srcPath = absPath(srcPath);
    id = hashPath(srcPath);

    string baseName = baseNameOf(srcPath);
    dstPath = canonPath(nixStore + "/" + (string) id + "-" + baseName);

    try {
        /* !!! should not use the substitutes! */
        dstPath = expandId(id, deterministicName ? dstPath : "", nixStore);
        return;
    } catch (...) {
    }
    
    copyPath(srcPath, dstPath);
    registerPath(dstPath, id);
}


void deleteFromStore(const string & path)
{
    string prefix =  + "/";
    if (!isInPrefix(path, nixStore))
        throw Error(format("path %1% is not in the store") % path);

    unregisterPath(path);

    deletePath(path);
}


void verifyStore()
{
    Strings paths;
    enumDB(nixDB, dbPath2Id, paths);

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
            if (!queryDB(nixDB, dbPath2Id, path, id)) abort();

            Strings idPaths;
            queryListDB(nixDB, dbId2Paths, id, idPaths);

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

        if (erase) delDB(nixDB, dbPath2Id, path);
    }

    Strings ids;
    enumDB(nixDB, dbId2Paths, ids);

    for (Strings::iterator i = ids.begin();
         i != ids.end(); i++)
    {
        FSId id = parseHash(*i);

        Strings idPaths;
        queryListDB(nixDB, dbId2Paths, id, idPaths);

        for (Strings::iterator j = idPaths.begin();     
             j != idPaths.end(); )
        {
            string id2;
            if (!queryDB(nixDB, dbPath2Id, *j, id2) || 
                id != parseHash(id2)) {
                debug(format("erasing path `%1%' from mapping for id %2%") 
                    % *j % (string) id);
                j = idPaths.erase(j);
            } else j++;
        }

        setListDB(nixDB, dbId2Paths, id, idPaths);
    }

    
    Strings subs;
    enumDB(nixDB, dbSubstitutes, subs);

    for (Strings::iterator i = subs.begin();
         i != subs.end(); i++)
    {
        FSId srcId = parseHash(*i);

        Strings subIds;
        queryListDB(nixDB, dbSubstitutes, srcId, subIds);

        for (Strings::iterator j = subIds.begin();     
             j != subIds.end(); )
        {
            FSId subId = parseHash(*j);
            
            Strings subPaths;
            queryListDB(nixDB, dbId2Paths, subId, subPaths);
            if (subPaths.size() == 0) {
                debug(format("erasing substitute %1% for %2%") 
                    % (string) subId % (string) srcId);
                j = subIds.erase(j);
            } else j++;
        }

        setListDB(nixDB, dbSubstitutes, srcId, subIds);
    }

    Strings sucs;
    enumDB(nixDB, dbSuccessors, sucs);

    for (Strings::iterator i = sucs.begin();
         i != sucs.end(); i++)
    {
        FSId id1 = parseHash(*i);

        string id2;
        if (!queryDB(nixDB, dbSuccessors, id1, id2)) abort();
        
        Strings id2Paths;
        queryListDB(nixDB, dbId2Paths, id2, id2Paths);
        if (id2Paths.size() == 0) {
            Strings id2Subs;
            queryListDB(nixDB, dbSubstitutes, id2, id2Subs);
            if (id2Subs.size() == 0) {
                debug(format("successor %1% for %2% missing") 
                    % id2 % (string) id1);
                delDB(nixDB, dbSuccessors, (string) id1);
            }
        }
    }
}
