#include "globals.hh"
#include "misc.hh"
#include "pathlocks.hh"
#include "local-store.hh"

#include <boost/shared_ptr.hpp>

#include <functional>
#include <queue>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __CYGWIN__
#include <windows.h>
#include <sys/cygwin.h>
#endif


namespace nix {


static string gcLockName = "gc.lock";
static string tempRootsDir = "temproots";
static string gcRootsDir = "gcroots";

static const int defaultGcLevel = 1000;


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

    if (!lockFile(fdGCLock, lockType, false)) {
        printMsg(lvlError, format("waiting for the big garbage collector lock..."));
        lockFile(fdGCLock, lockType, true);
    }

    /* !!! Restrict read permission on the GC root.  Otherwise any
       process that can open the file for reading can DoS the
       collector. */
    
    return fdGCLock.borrow();
}


void createSymlink(const Path & link, const Path & target, bool careful)
{
    /* Create directories up to `gcRoot'. */
    createDirs(dirOf(link));

    /* !!! shouldn't removing and creating the symlink be atomic? */

    /* Remove the old symlink. */
    if (pathExists(link)) {
        if (careful && (!isLink(link) || !isInStore(readLink(link))))
            throw Error(format("cannot create symlink `%1%'; already exists") % link);
        unlink(link.c_str());
    }

    /* And create the new one. */
    if (symlink(target.c_str(), link.c_str()) == -1)
        throw SysError(format("symlinking `%1%' to `%2%'")
            % link % target);
}


void LocalStore::syncWithGC()
{
    AutoCloseFD fdGCLock = openGCLock(ltRead);
}


void LocalStore::addIndirectRoot(const Path & path)
{
    string hash = printHash32(hashString(htSHA1, path));
    Path realRoot = canonPath((format("%1%/%2%/auto/%3%")
        % nixStateDir % gcRootsDir % hash).str());
    createSymlink(realRoot, path, false);
}


Path addPermRoot(const Path & _storePath, const Path & _gcRoot,
    bool indirect, bool allowOutsideRootsDir)
{
    Path storePath(canonPath(_storePath));
    Path gcRoot(canonPath(_gcRoot));
    assertStorePath(storePath);

    if (isInStore(gcRoot))
        throw Error(format(
                "creating a garbage collector root (%1%) in the Nix store is forbidden "
                "(are you running nix-build inside the store?)") % gcRoot);

    if (indirect) {
        createSymlink(gcRoot, storePath, true);
        store->addIndirectRoot(gcRoot);
    }

    else {
        if (!allowOutsideRootsDir) {
            Path rootsDir = canonPath((format("%1%/%2%") % nixStateDir % gcRootsDir).str());
    
            if (string(gcRoot, 0, rootsDir.size() + 1) != rootsDir + "/")
                throw Error(format(
                    "path `%1%' is not a valid garbage collector root; "
                    "it's not in the directory `%2%'")
                    % gcRoot % rootsDir);
        }
            
        createSymlink(gcRoot, storePath, false);
    }

    /* Check that the root can be found by the garbage collector.
       !!! This can be very slow on machines that have many roots.
       Instead of reading all the roots, it would be more efficient to
       check if the root is in a directory in or linked from the
       gcroots directory. */
    if (queryBoolSetting("gc-check-reachability", true)) {
        Roots roots = store->findRoots();
        if (roots.find(gcRoot) == roots.end())
            printMsg(lvlError, 
                format(
                    "warning: `%1%' is not in a directory where the garbage collector looks for roots; "
                    "therefore, `%2%' might be removed by the garbage collector")
                % gcRoot % storePath);
    }
        
    /* Grab the global GC root, causing us to block while a GC is in
       progress.  This prevents the set of permanent roots from
       increasing while a GC is in progress. */
    store->syncWithGC();
    
    return gcRoot;
}


/* The file to which we write our temporary roots. */
static Path fnTempRoots;
static AutoCloseFD fdTempRoots;


void LocalStore::addTempRoot(const Path & path)
{
    /* Create the temporary roots file for this process. */
    if (fdTempRoots == -1) {

        while (1) {
            Path dir = (format("%1%/%2%") % nixStateDir % tempRootsDir).str();
            createDirs(dir);
            
            fnTempRoots = (format("%1%/%2%")
                % dir % getpid()).str();

            AutoCloseFD fdGCLock = openGCLock(ltRead);
            
            if (pathExists(fnTempRoots))
                /* It *must* be stale, since there can be no two
                   processes with the same pid. */
                deletePath(fnTempRoots);

	    fdTempRoots = openLockFile(fnTempRoots, true);

            fdGCLock.close();
      
	    /* Note that on Cygwin a lot of the following complexity
	       is unnecessary, since we cannot delete open lock
	       files.  If we have the lock file open, then it's valid;
	       if we can delete it, then it wasn't in use any more. 

	       Also note that on Cygwin we cannot "upgrade" a lock
	       from a read lock to a write lock. */

#ifndef __CYGWIN__
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

#else
            break;
#endif
        }

    }

    /* Upgrade the lock to a write lock.  This will cause us to block
       if the garbage collector is holding our lock. */
    debug(format("acquiring write lock on `%1%'") % fnTempRoots);
    lockFile(fdTempRoots, ltWrite, true);

    string s = path + '\0';
    writeFull(fdTempRoots, (const unsigned char *) s.c_str(), s.size());

#ifndef __CYGWIN__
    /* Downgrade to a read lock. */
    debug(format("downgrading to read lock on `%1%'") % fnTempRoots);
    lockFile(fdTempRoots, ltRead, true);
#else
    debug(format("releasing write lock on `%1%'") % fnTempRoots);
    lockFile(fdTempRoots, ltNone, true);
#endif
}


void removeTempRoots()
{
    if (fdTempRoots != -1) {
        fdTempRoots.close();
        unlink(fnTempRoots.c_str());
    }
}


typedef boost::shared_ptr<AutoCloseFD> FDPtr;
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

#ifdef __CYGWIN__
	/* On Cygwin we just try to delete the lock file. */
	char win32Path[MAX_PATH];
	cygwin_conv_to_full_win32_path(path.c_str(), win32Path);
	if (DeleteFile(win32Path)) {
            printMsg(lvlError, format("removed stale temporary roots file `%1%'")
                % path);
            continue;
        } else
            debug(format("delete of `%1%' failed: %2%") % path % GetLastError());
#endif

        FDPtr fd(new AutoCloseFD(open(path.c_str(), O_RDWR, 0666)));
        if (*fd == -1) {
            /* It's okay if the file has disappeared. */
            if (errno == ENOENT) continue;
            throw SysError(format("opening temporary roots file `%1%'") % path);
        }

        /* This should work, but doesn't, for some reason. */
        //FDPtr fd(new AutoCloseFD(openLockFile(path, false)));
        //if (*fd == -1) continue;

#ifndef __CYGWIN__
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
#endif

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
    bool deleteStale, Roots & roots)
{
    try {
        
        struct stat st;
        if (lstat(path.c_str(), &st) == -1)
            throw SysError(format("statting `%1%'") % path);

        printMsg(lvlVomit, format("looking at `%1%'") % path);

        if (S_ISDIR(st.st_mode)) {
            Strings names = readDirectory(path);
            for (Strings::iterator i = names.begin(); i != names.end(); ++i)
                findRoots(path + "/" + *i, recurseSymlinks, deleteStale, roots);
        }

        else if (S_ISLNK(st.st_mode)) {
            Path target = absPath(readLink(path), dirOf(path));

            if (isInStore(target)) {
                debug(format("found root `%1%' in `%2%'")
                    % target % path);
                Path storePath = toStorePath(target);
                if (store->isValidPath(storePath)) 
                    roots[path] = storePath;
                else
                    printMsg(lvlInfo, format("skipping invalid root from `%1%' to `%2%'")
                        % path % storePath);
            }

            else if (recurseSymlinks) {
                if (pathExists(target))
                    findRoots(target, false, deleteStale, roots);
                else if (deleteStale) {
                    printMsg(lvlInfo, format("removing stale link from `%1%' to `%2%'") % path % target);
                    /* Note that we only delete when recursing, i.e.,
                       when we are still in the `gcroots' tree.  We
                       never delete stuff outside that tree. */
                    unlink(path.c_str());
                }
            }
        }

    }

    catch (SysError & e) {
        /* We only ignore permanent failures. */
        if (e.errNo == EACCES || e.errNo == ENOENT || e.errNo == ENOTDIR)
            printMsg(lvlInfo, format("cannot read potential root `%1%'") % path);
        else
            throw;
    }
}


static Roots findRoots(bool deleteStale)
{
    Roots roots;
    Path rootsDir = canonPath((format("%1%/%2%") % nixStateDir % gcRootsDir).str());
    findRoots(rootsDir, true, deleteStale, roots);
    return roots;
}


Roots LocalStore::findRoots()
{
    return nix::findRoots(false);
}


static void addAdditionalRoots(PathSet & roots)
{
    Path rootFinder = getEnv("NIX_ROOT_FINDER",
        nixLibexecDir + "/nix/find-runtime-roots.pl");

    if (rootFinder.empty()) return;
    
    debug(format("executing `%1%' to find additional roots") % rootFinder);

    string result = runProgram(rootFinder);

    Strings paths = tokenizeString(result, "\n");
    
    for (Strings::iterator i = paths.begin(); i != paths.end(); ++i) {
        if (isInStore(*i)) {
            Path path = toStorePath(*i);
            if (roots.find(path) == roots.end() && store->isValidPath(path)) {
                debug(format("found additional root `%1%'") % path);
                roots.insert(path);
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
    if (store->isValidPath(path))
        store->queryReferences(path, references);
    
    for (PathSet::iterator i = references.begin();
         i != references.end(); ++i)
        /* Don't traverse into paths that don't exist.  That can
           happen due to substitutes for non-existent paths. */
        if (*i != path && paths.find(*i) != paths.end())
            dfsVisit(paths, *i, visited, sorted);

    sorted.push_front(path);
}


Paths topoSortPaths(const PathSet & paths)
{
    Paths sorted;
    PathSet visited;
    for (PathSet::const_iterator i = paths.begin(); i != paths.end(); ++i)
        dfsVisit(paths, *i, visited, sorted);
    return sorted;
}


static time_t lastFileAccessTime(const Path & path)
{
    checkInterrupt();
    
    struct stat st;
    if (lstat(path.c_str(), &st) == -1)
        throw SysError(format("statting `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
        time_t last = 0;
	Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); ++i) {
            time_t t = lastFileAccessTime(path + "/" + *i);
            if (t > last) last = t;
        }
        return last;
    }

    else if (S_ISLNK(st.st_mode)) return 0;

    else return st.st_atime;
}


struct GCLimitReached { };


void LocalStore::gcPath(const GCOptions & options, GCResults & results, 
    const Path & path)
{
    results.paths.insert(path);

    if (!pathExists(path)) return;
                
    /* Okay, it's safe to delete. */
    unsigned long long bytesFreed, blocksFreed;
    deleteFromStore(path, bytesFreed, blocksFreed);
    results.bytesFreed += bytesFreed;
    results.blocksFreed += blocksFreed;

    if (options.maxFreed && results.bytesFreed > options.maxFreed) {
        printMsg(lvlInfo, format("deleted more than %1% bytes; stopping") % options.maxFreed);
        throw GCLimitReached();
    }

    if (options.maxLinks) {
        struct stat st;
        if (stat(nixStore.c_str(), &st) == -1)
            throw SysError(format("statting `%1%'") % nixStore);
        if (st.st_nlink < options.maxLinks) {
            printMsg(lvlInfo, format("link count on the store has dropped below %1%; stopping") % options.maxLinks);
            throw GCLimitReached();
        }
    }
}


void LocalStore::gcPathRecursive(const GCOptions & options,
    GCResults & results, PathSet & done, const Path & path)
{
    if (done.find(path) != done.end()) return;
    done.insert(path);

    startNest(nest, lvlDebug, format("looking at `%1%'") % path);
        
    /* Delete all the referrers first.  They must be garbage too,
       since if they were live, then the current path would also be
       live.  Note that deleteFromStore() below still makes sure that
       the referrer set has become empty, just in case.  (However that
       doesn't guard against deleting top-level paths that are only
       reachable from GC roots.) */
    PathSet referrers;
    if (isValidPath(path))
        queryReferrers(path, referrers);
    foreach (PathSet::iterator, i, referrers)
        if (*i != path) gcPathRecursive(options, results, done, *i);

    printMsg(lvlInfo, format("deleting `%1%'") % path);
            
    gcPath(options, results, path);
}


struct CachingAtimeComparator : public std::binary_function<Path, Path, bool> 
{
    std::map<Path, time_t> cache;

    time_t lookup(const Path & p)
    {
        std::map<Path, time_t>::iterator i = cache.find(p);
        if (i != cache.end()) return i->second;
        debug(format("computing atime of `%1%'") % p);
        cache[p] = lastFileAccessTime(p);
        assert(cache.find(p) != cache.end());
        return cache[p];
    }
        
    bool operator () (const Path & p1, const Path & p2)
    {
        return lookup(p2) < lookup(p1);
    }
};


static string showTime(const string & format, time_t t)
{
    char s[128];
    strftime(s, sizeof s, format.c_str(), localtime(&t));
    return string(s);
}


static bool isLive(const Path & path, const PathSet & livePaths,
    const PathSet & tempRoots, const PathSet & tempRootsClosed)
{
    if (livePaths.find(path) != livePaths.end() ||
        tempRootsClosed.find(path) != tempRootsClosed.end()) return true;

    /* A lock file belonging to a path that we're building right
       now isn't garbage. */
    if (hasSuffix(path, ".lock") && tempRoots.find(string(path, 0, path.size() - 5)) != tempRoots.end())
        return true;

    /* Don't delete .chroot directories for derivations that are
       currently being built. */
    if (hasSuffix(path, ".chroot") && tempRoots.find(string(path, 0, path.size() - 7)) != tempRoots.end())
        return true;

    return false;
}


void LocalStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    bool gcKeepOutputs =
        queryBoolSetting("gc-keep-outputs", false);
    bool gcKeepDerivations =
        queryBoolSetting("gc-keep-derivations", true);
    int gcKeepOutputsThreshold = 
        queryIntSetting ("gc-keep-outputs-threshold", defaultGcLevel);

    /* Acquire the global GC root.  This prevents
       a) New roots from being added.
       b) Processes from creating new temporary root files. */
    AutoCloseFD fdGCLock = openGCLock(ltWrite);

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    printMsg(lvlError, format("finding garbage collector roots..."));
    Roots rootMap = options.ignoreLiveness ? Roots() : nix::findRoots(true);

    PathSet roots;
    for (Roots::iterator i = rootMap.begin(); i != rootMap.end(); ++i)
        roots.insert(i->second);

    /* Add additional roots returned by the program specified by the
       NIX_ROOT_FINDER environment variable.  This is typically used
       to add running programs to the set of roots (to prevent them
       from being garbage collected). */
    if (!options.ignoreLiveness)
        addAdditionalRoots(roots);

    if (options.action == GCOptions::gcReturnRoots) {
        results.paths = roots;
        return;
    }

    /* Determine the live paths which is just the closure of the
       roots under the `references' relation. */
    printMsg(lvlError, format("computing live paths..."));
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
            Path deriver = queryDeriver(*i);
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

		string gcLevelStr = drv.env["__gcLevel"];
		int gcLevel;
		if (!string2Int(gcLevelStr, gcLevel))
		    gcLevel = defaultGcLevel;
		
		if (gcLevel >= gcKeepOutputsThreshold)    
		    for (DerivationOutputs::iterator j = drv.outputs.begin();
                         j != drv.outputs.end(); ++j)
			if (isValidPath(j->second.path))
			    computeFSClosure(j->second.path, livePaths);
            }
    }

    if (options.action == GCOptions::gcReturnLive) {
        results.paths = livePaths;
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

    /* After this point the set of roots or temporary roots cannot
       increase, since we hold locks on everything.  So everything
       that is not currently in in `livePaths' or `tempRootsClosed'
       can be deleted. */
    
    /* Read the Nix store directory to find all currently existing
       paths and filter out all live paths. */
    printMsg(lvlError, format("reading the Nix store..."));
    PathSet storePaths;
    
    if (options.action != GCOptions::gcDeleteSpecific) {
        Paths entries = readDirectory(nixStore);
        foreach (Paths::iterator, i, entries) {
            Path path = canonPath(nixStore + "/" + *i);
            if (!isLive(path, livePaths, tempRoots, tempRootsClosed)) storePaths.insert(path);
        }
    }

    else {
        foreach (PathSet::iterator, i, options.pathsToDelete) {
            assertStorePath(*i);
            storePaths.insert(*i);
            if (isLive(*i, livePaths, tempRoots, tempRootsClosed))
                throw Error(format("cannot delete path `%1%' since it is still alive") % *i);
        }
    }

    if (options.action == GCOptions::gcReturnDead) {
        results.paths.insert(storePaths.begin(), storePaths.end());
        return;
    }

    /* Delete all dead store paths (or until one of the stop
       conditions is reached). */

    PathSet done;
    try {

        if (!options.useAtime) {
            /* Delete the paths, respecting the partial ordering
               determined by the references graph. */
            printMsg(lvlError, format("deleting garbage..."));
            foreach (PathSet::iterator, i, storePaths)
                gcPathRecursive(options, results, done, *i);
        }

        else {

            /* Delete in order of ascending last access time, still
               maintaining the partial ordering of the reference
               graph.  Note that we can't use a topological sort for
               this because that takes time O(V+E), and in this case
               E=O(V^2) (i.e. the graph is dense because of the edges
               due to the atime ordering).  So instead we put all
               deletable paths in a priority queue (ordered by atime),
               and after deleting a path, add additional paths that
               have become deletable to the priority queue. */

            CachingAtimeComparator atimeComp;

            /* Create a priority queue that orders paths by ascending
               atime.  This is why C++ needs type inferencing... */
            std::priority_queue<Path, vector<Path>, binary_function_ref_adapter<CachingAtimeComparator> > prioQueue =
                std::priority_queue<Path, vector<Path>, binary_function_ref_adapter<CachingAtimeComparator> >(binary_function_ref_adapter<CachingAtimeComparator>(&atimeComp));

           /* Initially put the paths that are invalid or have no
              referrers into the priority queue. */
            printMsg(lvlError, format("finding deletable paths..."));
            foreach (PathSet::iterator, i, storePaths) {
                checkInterrupt();
                /* We can safely delete a path if it's invalid or
                   it has no referrers.  Note that all the invalid
                   paths will be deleted in the first round. */
                if (isValidPath(*i)) {
                    if (queryReferrersNoSelf(*i).empty()) prioQueue.push(*i);
                } else prioQueue.push(*i);
            }

            debug(format("%1% initially deletable paths") % prioQueue.size());

            /* Now delete everything in the order of the priority
               queue until nothing is left. */
            printMsg(lvlError, format("deleting garbage..."));
            while (!prioQueue.empty()) {
                checkInterrupt();
                Path path = prioQueue.top(); prioQueue.pop();

                if (options.maxAtime != (time_t) -1 &&
                    atimeComp.lookup(path) > options.maxAtime)
                    continue;
                
                printMsg(lvlInfo, format("deleting `%1%' (last accessed %2%)") % path % showTime("%F %H:%M:%S", atimeComp.lookup(path)));

                PathSet references;
                if (isValidPath(path)) references = queryReferencesNoSelf(path);

                gcPath(options, results, path);

                /* For each reference of the current path, see if the
                   reference has now become deletable (i.e. is in the
                   set of dead paths and has no referrers left).  If
                   so add it to the priority queue. */
                foreach (PathSet::iterator, i, references) {
                    if (storePaths.find(*i) != storePaths.end() &&
                        queryReferrersNoSelf(*i).empty())
                    {
                        debug(format("path `%1%' has become deletable") % *i);
                        prioQueue.push(*i);
                    }
                }
            }
            
        }
        
    } catch (GCLimitReached & e) {
    }
}


}
