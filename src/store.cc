#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>

#include "store.hh"
#include "globals.hh"
#include "db.hh"
#include "archive.hh"
#include "fstate.hh"


struct CopySink : DumpSink
{
    int fd;
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        if (write(fd, (char *) data, len) != (ssize_t) len)
            throw SysError("writing to child");
    }
};


struct CopySource : RestoreSource
{
    int fd;
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        ssize_t res = read(fd, (char *) data, len);
        if (res == -1)
            throw SysError("reading from parent");
        if (res != (ssize_t) len)
            throw Error("not enough data available on parent");
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


void registerSubstitute(const Hash & srcHash, const Hash & subHash)
{
    Strings subs;
    queryListDB(nixDB, dbSubstitutes, srcHash, subs); /* non-existence = ok */

    for (Strings::iterator it = subs.begin(); it != subs.end(); it++)
        if (parseHash(*it) == subHash) return;
    
    subs.push_back(subHash);
    
    setListDB(nixDB, dbSubstitutes, srcHash, subs);
}


Hash registerPath(const string & _path, Hash hash)
{
    string path(canonPath(_path));

    if (hash == Hash()) hash = hashPath(path);

    Strings paths;
    queryListDB(nixDB, dbHash2Paths, hash, paths); /* non-existence = ok */

    for (Strings::iterator it = paths.begin();
         it != paths.end(); it++)
        if (*it == path) goto exists;
    
    paths.push_back(path);
    
    setListDB(nixDB, dbHash2Paths, hash, paths);
    
 exists:
    return hash;
}


void unregisterPath(const string & _path)
{
    string path(canonPath(_path));

    Hash hash = hashPath(path);
    
    Strings paths, paths2;
    queryListDB(nixDB, dbHash2Paths, hash, paths); /* non-existence = ok */

    bool changed = false;
    for (Strings::iterator it = paths.begin();
         it != paths.end(); it++)
        if (*it != path) paths2.push_back(*it); else changed = true;

    if (changed)
        setListDB(nixDB, dbHash2Paths, hash, paths2);
}


bool isInPrefix(const string & path, const string & _prefix)
{
    string prefix = canonPath(_prefix + "/");
    return string(path, 0, prefix.size()) == prefix;
}


string expandHash(const Hash & hash, const string & target,
    const string & prefix)
{
    Strings paths;

    if (!target.empty() && !isInPrefix(target, prefix))
        abort();

    queryListDB(nixDB, dbHash2Paths, hash, paths);

    /* !!! we shouldn't check for staleness by default --- too slow */

    /* Pick one equal to `target'. */
    if (!target.empty()) {

        for (Strings::iterator i = paths.begin();
             i != paths.end(); i++)
        {
            string path = *i;
            try {
#if 0
                if (path == target && hashPath(path) == hash)
#endif
                if (path == target && pathExists(path))
                    return path;
            } catch (Error & e) {
                debug(format("stale path: %1%") % e.msg());
                /* try next one */
            }
        }
        
    }

    /* Arbitrarily pick the first one that exists and isn't stale. */
    for (Strings::iterator it = paths.begin();
         it != paths.end(); it++)
    {
        string path = *it;
        try {
            if (isInPrefix(path, prefix) && hashPath(path) == hash) {
                if (target.empty())
                    return path;
                else {
                    copyPath(path, target);
                    return target;
                }
            }
        } catch (Error & e) {
            debug(format("stale path: %1%") % e.msg());
            /* try next one */
        }
    }

    /* Try to realise the substitutes. */

    Strings subs;
    queryListDB(nixDB, dbSubstitutes, hash, subs); /* non-existence = ok */

    for (Strings::iterator it = subs.begin(); it != subs.end(); it++) {
        StringSet dummy;
        FState nf = realiseFState(hash2fstate(parseHash(*it)), dummy);
        string path = fstatePath(nf);

        if (hashPath(path) != hash)
            throw Error(format("bad substitute in `%1%'") % (string) path);

        if (target.empty())
            return path; /* !!! prefix */
        else {
            if (path != target) {
                copyPath(path, target);
                registerPath(target, hash);
            }
            return target;
        }
    }
    
    throw Error(format("cannot expand hash `%1%'") % (string) hash);
}

    
void addToStore(string srcPath, string & dstPath, Hash & hash,
    bool deterministicName)
{
    srcPath = absPath(srcPath);
    hash = hashPath(srcPath);

    string baseName = baseNameOf(srcPath);
    dstPath = canonPath(nixStore + "/" + (string) hash + "-" + baseName);

    try {
        /* !!! should not use the substitutes! */
        dstPath = expandHash(hash, deterministicName ? dstPath : "", nixStore);
        return;
    } catch (...) {
    }
    
    copyPath(srcPath, dstPath);
    registerPath(dstPath, hash);
}


void deleteFromStore(const string & path)
{
    string prefix =  + "/";
    if (!isInPrefix(path, nixStore))
        throw Error(format("path %1% is not in the store") % path);

    unregisterPath(path);

    deletePath(path);
}
