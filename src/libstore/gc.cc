#include "globals.hh"
#include "gc.hh"
#include "build.hh"
#include "pathlocks.hh"

#include <boost/shared_ptr.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


/* Acquire the global GC lock. */
static AutoCloseFD openGCLock(LockType lockType)
{
#if 0
    Path fnGCLock = (format("%1%/%2%/%3%")
        % nixStateDir % tempRootsDir % getpid()).str();
        
    fdTempRoots = open(fnTempRoots.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fdTempRoots == -1)
        throw SysError(format("opening temporary roots file `%1%'") % fnTempRoots);
#endif
}


static string tempRootsDir = "temproots";

/* The file to which we write our temporary roots. */
static Path fnTempRoots;
static AutoCloseFD fdTempRoots;


void addTempRoot(const Path & path)
{
    /* Create the temporary roots file for this process. */
    if (fdTempRoots == -1) {

        while (1) {
            fnTempRoots = (format("%1%/%2%/%3%")
                % nixStateDir % tempRootsDir % getpid()).str();
        
            fdTempRoots = open(fnTempRoots.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
            if (fdTempRoots == -1)
                throw SysError(format("opening temporary roots file `%1%'") % fnTempRoots);

            debug(format("acquiring read lock on `%1%'") % fnTempRoots);
            lockFile(fdTempRoots, ltRead, true);

            /* Check whether the garbage collector didn't get in our
               way. */
            struct stat st;
            if (fstat(fdTempRoots, &st) == -1)
                throw SysError(format("statting `%1%'") % fnTempRoots);
            if (st.st_size == 0) break;
            
            /* The garbage collector deleted this file before we could
               get a lock.  (It won't delete the file after we get a
               lock.)  Try again. */
        }

    }

    /* Upgrade the lock to a write lock.  This will cause us to block
       if the garbage collector is holding our lock. */
    debug(format("acquiring write lock on `%1%'") % fnTempRoots);
    lockFile(fdTempRoots, ltWrite, true);

    string s = path + '\0';
    writeFull(fdTempRoots, (const unsigned char *) s.c_str(), s.size());

    /* Downgrade to a read lock. */
    debug(format("downgrading to read lock on `%1%'") % fnTempRoots);
    lockFile(fdTempRoots, ltRead, true);
}


void removeTempRoots()
{
    if (fdTempRoots != -1) {
        fdTempRoots.close();
        unlink(fnTempRoots.c_str());
    }
}


typedef shared_ptr<AutoCloseFD> FDPtr;
typedef list<FDPtr> FDs;


static void readTempRoots(PathSet & tempRoots, FDs & fds)
{
    /* Read the `temproots' directory for per-process temporary root
       files. */
    Strings tempRootFiles = readDirectory(
        (format("%1%/%2%") % nixStateDir % tempRootsDir).str());

    for (Strings::iterator i = tempRootFiles.begin();
         i != tempRootFiles.end(); ++i)
    {
        Path path = (format("%1%/%2%/%3%") % nixStateDir % tempRootsDir % *i).str();

        debug(format("reading temporary root file `%1%'") % path);
            
        FDPtr fd(new AutoCloseFD(open(path.c_str(), O_RDWR, 0666)));
        if (*fd == -1) {
            /* It's okay if the file has disappeared. */
            if (errno == ENOENT) continue;
            throw SysError(format("opening temporary roots file `%1%'") % path);
        }

        /* Try to acquire a write lock without blocking.  This can
           only succeed if the owning process has died.  In that case
           we don't care about its temporary roots. */
        if (lockFile(*fd, ltWrite, false)) {
            printMsg(lvlError, format("removing stale temporary roots file `%1%'")
                % path);
            unlink(path.c_str());
            writeFull(*fd, (const unsigned char *) "d", 1);
            continue;
        }

        /* Acquire a read lock.  This will prevent the owning process
           from upgrading to a write lock, therefore it will block in
           addTempRoot(). */
        debug(format("waiting for read lock on `%1%'") % path);
        lockFile(*fd, ltRead, true);

        /* Read the entire file. */
        struct stat st;
        if (fstat(*fd, &st) == -1)
            throw SysError(format("statting `%1%'") % path);
        unsigned char buf[st.st_size]; /* !!! stack space */
        readFull(*fd, buf, st.st_size);

        /* Extract the roots. */
        string contents((char *) buf, st.st_size);
        unsigned int pos = 0, end;

        while ((end = contents.find((char) 0, pos)) != string::npos) {
            Path root(contents, pos, end - pos);
            debug(format("got temporary root `%1%'") % root);
            assertStorePath(root);
            tempRoots.insert(root);
            pos = end + 1;
        }

        fds.push_back(fd); /* keep open */
    }
}


static void dfsVisit(const PathSet & paths, const Path & path,
    PathSet & visited, Paths & sorted)
{
    if (visited.find(path) != visited.end()) return;
    visited.insert(path);
    
    PathSet references;
    if (isValidPath(path))
        queryReferences(path, references);
    
    for (PathSet::iterator i = references.begin();
         i != references.end(); ++i)
        /* Don't traverse into paths that don't exist.  That can
           happen due to substitutes for non-existent paths. */
        if (*i != path && paths.find(*i) != paths.end())
            dfsVisit(paths, *i, visited, sorted);

    sorted.push_front(path);
}


static Paths topoSort(const PathSet & paths)
{
    Paths sorted;
    PathSet visited;
    for (PathSet::const_iterator i = paths.begin(); i != paths.end(); ++i)
        dfsVisit(paths, *i, visited, sorted);
    return sorted;
}


void collectGarbage(const PathSet & roots, GCAction action,
    PathSet & result)
{
    result.clear();
    
    /* !!! TODO: Acquire the global GC root.  This prevents
       a) New roots from being added.
       b) Processes from creating new temporary root files. */

    /* !!! Restrict read permission on the GC root.  Otherwise any
       process that can open the file for reading can DoS the
       collector. */
    
    /* Determine the live paths which is just the closure of the
       roots under the `references' relation. */
    PathSet livePaths;
    for (PathSet::const_iterator i = roots.begin(); i != roots.end(); ++i)
        computeFSClosure(canonPath(*i), livePaths);

    if (action == gcReturnLive) {
        result = livePaths;
        return;
    }

    /* Read the temporary roots.  This acquires read locks on all
       per-process temporary root files.  So after this point no paths
       can be added to the set of temporary roots. */
    PathSet tempRoots;
    FDs fds;
    readTempRoots(tempRoots, fds);

    /* Close the temporary roots.  Note that we *cannot* do this in
       readTempRoots(), because there we may not have all locks yet,
       meaning that an invalid path can become valid (and thus add to
       the references graph) after we have added it to the closure
       (and computeFSClosure() assumes that the presence of a path
       means that it has already been closed). */
    PathSet tempRootsClosed;
    for (PathSet::iterator i = tempRoots.begin(); i != tempRoots.end(); ++i)
        if (isValidPath(*i))
            computeFSClosure(*i, tempRootsClosed);
        else
            tempRootsClosed.insert(*i);

    /* For testing - see tests/gc-concurrent.sh. */
    if (getenv("NIX_DEBUG_GC_WAIT"))
        sleep(2);
    
    /* After this point the set of roots or temporary roots cannot
       increase, since we hold locks on everything.  So everything
       that is not currently in in `livePaths' or `tempRootsClosed'
       can be deleted. */
    
    /* Read the Nix store directory to find all currently existing
       paths. */
    Paths storePaths = readDirectory(nixStore);
    PathSet storePaths2;
    for (Paths::iterator i = storePaths.begin(); i != storePaths.end(); ++i)
        storePaths2.insert(canonPath(nixStore + "/" + *i));

    /* Topologically sort them under the `referers' relation.  That
       is, a < b iff a is in referers(b).  This gives us the order in
       which things can be deleted safely. */
    /* !!! when we have multiple output paths per derivation, this
       will not work anymore because we get cycles. */
    storePaths = topoSort(storePaths2);

    for (Paths::iterator i = storePaths.begin(); i != storePaths.end(); ++i) {

        debug(format("considering deletion of `%1%'") % *i);
        
        if (livePaths.find(*i) != livePaths.end()) {
            debug(format("live path `%1%'") % *i);
            continue;
        }

        if (tempRootsClosed.find(*i) != tempRootsClosed.end()) {
            debug(format("temporary root `%1%'") % *i);
            continue;
        }

        debug(format("dead path `%1%'") % *i);
        result.insert(*i);

        AutoCloseFD fdLock;

        if (action == gcDeleteDead) {

            /* Only delete a lock file if we can acquire a write lock
               on it.  That means that it's either stale, or the
               process that created it hasn't locked it yet.  In the
               latter case the other process will detect that we
               deleted the lock, and retry (see pathlocks.cc). */
            if (i->size() >= 5 && string(*i, i->size() - 5) == ".lock") {

                fdLock = open(i->c_str(), O_RDWR);
                if (fdLock == -1) {
                    if (errno == ENOENT) continue;
                    throw SysError(format("opening lock file `%1%'") % *i);
                }

                if (!lockFile(fdLock, ltWrite, false)) {
                    debug(format("skipping active lock `%1%'") % *i);
                    continue;
                }
            }

            printMsg(lvlInfo, format("deleting `%1%'") % *i);
            
            /* Okay, it's safe to delete. */
            deleteFromStore(*i);

            if (fdLock != -1)
                /* Write token to stale (deleted) lock file. */
                writeFull(fdLock, (const unsigned char *) "d", 1);
        }
    }
}
