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


static string tempRootsDir = "temproots";

/* The file to which we write our temporary roots. */
Path fnTempRoots;
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
            /* !!! write token, unlink */
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
        debug(format("FILE SIZE %1%") % st.st_size);

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

    for (FDs::iterator i = fds.begin(); i != fds.end(); ++i)
        debug(format("FD %1%") % (int) **i);
    
    /* !!! TODO: Try to acquire (without blocking) exclusive locks on
       the files in the `pending' directory.  Delete all files for
       which we managed to acquire such a lock (since if we could get
       such a lock, that means that the process that owned the file
       has died). */

    /* !!! TODO: Acquire shared locks on all files in the pending
       directories.  This prevents the set of pending paths from
       increasing while we are garbage-collecting.  Read the set of
       pending paths from those files. */

    /* Read the Nix store directory to find all currently existing
       paths. */
    Strings storeNames = readDirectory(nixStore);

    for (Strings::iterator i = storeNames.begin(); i != storeNames.end(); ++i) {
        Path path = canonPath(nixStore + "/" + *i);

        if (livePaths.find(path) != livePaths.end()) {
            debug(format("live path `%1%'") % path);
            continue;
        }

        if (tempRoots.find(path) != tempRoots.end()) {
            debug(format("temporary root `%1%'") % path);
            continue;
        }

        debug(format("dead path `%1%'") % path);
        result.insert(path);

        if (action == gcDeleteDead) {
            printMsg(lvlInfo, format("deleting `%1%'") % path);
            deleteFromStore(path);
        }

        /* Only delete lock files if the path is belongs to doesn't
           exist and isn't a temporary root and we can acquire an
           exclusive lock on it. */
        /* !!! */
    }
}
