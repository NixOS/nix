#include "globals.hh"
#include "gc.hh"
#include "misc.hh"
#include "pathlocks.hh"

#include <boost/shared_ptr.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


static string gcLockName = "gc.lock";
static string tempRootsDir = "temproots";
static string gcRootsDir = "gcroots";


/* Acquire the global GC lock.  This is used to prevent new Nix
   processes from starting after the temporary root files have been
   read.  To be precise: when they try to create a new temporary root
   file, they will block until the garbage collector has finished /
   yielded the GC lock. */
static int openGCLock(LockType lockType)
{
    Path fnGCLock = (format("%1%/%2%")
        % nixStateDir % gcLockName).str();
         
    debug(format("acquiring global GC lock `%1%'") % fnGCLock);
    
    AutoCloseFD fdGCLock = open(fnGCLock.c_str(), O_RDWR | O_CREAT, 0600);
    if (fdGCLock == -1)
        throw SysError(format("opening global GC lock `%1%'") % fnGCLock);

    lockFile(fdGCLock, lockType, true);

    /* !!! Restrict read permission on the GC root.  Otherwise any
       process that can open the file for reading can DoS the
       collector. */
    
    return fdGCLock.borrow();
}


void createSymlink(const Path & link, const Path & target, bool careful)
{
    /* Create directories up to `gcRoot'. */
    createDirs(dirOf(link));

    /* Remove the old symlink. */
    if (pathExists(link)) {
        if (careful && (!isLink(link) || !isInStore(readLink(link))))
            throw Error(format("cannot create symlink `%1%'; already exists") % link);
        unlink(link.c_str());
    }

    /* And create the new own. */
    if (symlink(target.c_str(), link.c_str()) == -1)
        throw SysError(format("symlinking `%1%' to `%2%'")
            % link % target);
}


Path addPermRoot(const Path & _storePath, const Path & _gcRoot,
    bool indirect)
{
    Path storePath(canonPath(_storePath));
    Path gcRoot(canonPath(_gcRoot));
    assertStorePath(storePath);

    /* Grab the global GC root.  This prevents the set of permanent
       roots from increasing while a GC is in progress. */
    AutoCloseFD fdGCLock = openGCLock(ltRead);

    if (indirect) {
        string hash = printHash32(hashString(htSHA1, gcRoot));
        Path realRoot = canonPath((format("%1%/%2%/auto/%3%")
            % nixStateDir % gcRootsDir % hash).str());
        
        {
            SwitchToOriginalUser sw;
            createSymlink(gcRoot, storePath, true);
        }
        createSymlink(realRoot, gcRoot, false);
    }

    else {
        Path rootsDir = canonPath((format("%1%/%2%") % nixStateDir % gcRootsDir).str());
    
        if (string(gcRoot, 0, rootsDir.size() + 1) != rootsDir + "/")
            throw Error(format(
                "path `%1%' is not a valid garbage collector root; "
                "it's not in the directory `%2%'")
                % gcRoot % rootsDir);

        createSymlink(gcRoot, storePath, false);
    }

    return gcRoot;
}


/* The file to which we write our temporary roots. */
static Path fnTempRoots;
static AutoCloseFD fdTempRoots;


void addTempRoot(const Path & path)
{
    /* Create the temporary roots file for this process. */
    if (fdTempRoots == -1) {

        while (1) {
            Path dir = (format("%1%/%2%") % nixStateDir % tempRootsDir).str();
            createDirs(dir);
            
            fnTempRoots = (format("%1%/%2%")
                % dir % getpid()).str();

            AutoCloseFD fdGCLock = openGCLock(ltRead);
            
            fdTempRoots = open(fnTempRoots.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
            if (fdTempRoots == -1)
                throw SysError(format("opening temporary roots file `%1%'") % fnTempRoots);

            fdGCLock.close();
            
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
        string contents = readFile(*fd);

        /* Extract the roots. */
        string::size_type pos = 0, end;

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


static void findRoots(const Path & path, bool recurseSymlinks,
    PathSet & roots)
{
    struct stat st;
    if (lstat(path.c_str(), &st) == -1)
        throw SysError(format("statting `%1%'") % path);

    printMsg(lvlVomit, format("looking at `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
	Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            findRoots(path + "/" + *i, recurseSymlinks, roots);
    }

    else if (S_ISLNK(st.st_mode)) {
        string target = readLink(path);
        Path target2 = absPath(target, dirOf(path));

        if (isInStore(target2)) {
            debug(format("found root `%1%' in `%2%'")
                % target2 % path);
            Path target3 = toStorePath(target2);
            if (isValidPath(target3)) 
                roots.insert(target3);
            else
                printMsg(lvlInfo, format("skipping invalid root from `%1%' to `%2%'")
                    % path % target3);
        }

        else if (recurseSymlinks) {
            if (pathExists(target2))
                findRoots(target2, false, roots);
            else {
                printMsg(lvlInfo, format("removing stale link from `%1%' to `%2%'") % path % target2);
                /* Note that we only delete when recursing, i.e., when
                   we are still in the `gcroots' tree.  We never
                   delete stuff outside that tree. */
                unlink(path.c_str());
            }
        }
    }
}


static void dfsVisit(const PathSet & paths, const Path & path,
    PathSet & visited, Paths & sorted)
{
    if (visited.find(path) != visited.end()) return;
    visited.insert(path);
    
    PathSet references;
    if (isValidPath(path))
        queryReferences(noTxn, path, references);
    
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


void collectGarbage(GCAction action, const PathSet & pathsToDelete,
    bool ignoreLiveness, PathSet & result, unsigned long long & bytesFreed)
{
    result.clear();
    bytesFreed = 0;

    bool gcKeepOutputs =
        queryBoolSetting("gc-keep-outputs", false);
    bool gcKeepDerivations =
        queryBoolSetting("gc-keep-derivations", true);

    /* Acquire the global GC root.  This prevents
       a) New roots from being added.
       b) Processes from creating new temporary root files. */
    AutoCloseFD fdGCLock = openGCLock(ltWrite);

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    Path rootsDir = canonPath((format("%1%/%2%") % nixStateDir % gcRootsDir).str());
    PathSet roots;
    if (!ignoreLiveness)
        findRoots(rootsDir, true, roots);

    if (action == gcReturnRoots) {
        result = roots;
        return;
    }

    /* Determine the live paths which is just the closure of the
       roots under the `references' relation. */
    PathSet livePaths;
    for (PathSet::const_iterator i = roots.begin(); i != roots.end(); ++i)
        computeFSClosure(canonPath(*i), livePaths);

    if (gcKeepDerivations) {
        for (PathSet::iterator i = livePaths.begin();
             i != livePaths.end(); ++i)
        {
            /* Note that the deriver need not be valid (e.g., if we
               previously ran the collector with `gcKeepDerivations'
               turned off). */
            Path deriver = queryDeriver(noTxn, *i);
            if (deriver != "" && isValidPath(deriver))
                computeFSClosure(deriver, livePaths);
        }
    }

    if (gcKeepOutputs) {
        /* Hmz, identical to storePathRequisites in nix-store. */
        for (PathSet::iterator i = livePaths.begin();
             i != livePaths.end(); ++i)
            if (isDerivation(*i)) {
                Derivation drv = derivationFromPath(*i);
                for (DerivationOutputs::iterator j = drv.outputs.begin();
                     j != drv.outputs.end(); ++j)
                    if (isValidPath(j->second.path))
                        computeFSClosure(j->second.path, livePaths);
            }
    }

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
    PathSet storePathSet;
    if (action != gcDeleteSpecific) {
        Paths entries = readDirectory(nixStore);
        for (Paths::iterator i = entries.begin(); i != entries.end(); ++i)
            storePathSet.insert(canonPath(nixStore + "/" + *i));
    } else {
        for (PathSet::iterator i = pathsToDelete.begin();
             i != pathsToDelete.end(); ++i)
        {
            assertStorePath(*i);
            storePathSet.insert(*i);
        }
    }

    /* Topologically sort them under the `referrers' relation.  That
       is, a < b iff a is in referrers(b).  This gives us the order in
       which things can be deleted safely. */
    /* !!! when we have multiple output paths per derivation, this
       will not work anymore because we get cycles. */
    Paths storePaths = topoSort(storePathSet);

    /* Try to delete store paths in the topologically sorted order. */
    for (Paths::iterator i = storePaths.begin(); i != storePaths.end(); ++i) {

        debug(format("considering deletion of `%1%'") % *i);
        
        if (livePaths.find(*i) != livePaths.end()) {
            if (action == gcDeleteSpecific)
                throw Error(format("cannot delete path `%1%' since it is still alive") % *i);
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

        if (action == gcDeleteDead || action == gcDeleteSpecific) {

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
            unsigned long long freed;
            deleteFromStore(*i, freed);
            bytesFreed += freed;

            if (fdLock != -1)
                /* Write token to stale (deleted) lock file. */
                writeFull(fdLock, (const unsigned char *) "d", 1);
        }
    }
}
