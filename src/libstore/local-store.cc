#include "config.h"
#include "local-store.hh"
#include "globals.hh"
#include "archive.hh"
#include "pathlocks.hh"
#include "aterm.hh"
#include "derivations-ast.hh"
#include "worker-protocol.hh"
    
#include <iostream>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>
#include <errno.h>


namespace nix {

    
void checkStoreNotSymlink()
{
    if (getEnv("NIX_IGNORE_SYMLINK_STORE") == "1") return;
    Path path = nixStore;
    struct stat st;
    while (path != "/") {
        if (lstat(path.c_str(), &st))
            throw SysError(format("getting status of `%1%'") % path);
        if (S_ISLNK(st.st_mode))
            throw Error(format(
                "the path `%1%' is a symlink; "
                "this is not allowed for the Nix store and its parent directories")
                % path);
        path = dirOf(path);
    }
}


LocalStore::LocalStore()
{
    substitutablePathsLoaded = false;
    
    schemaPath = nixDBPath + "/schema";
    
    if (readOnlyMode) return;

    checkStoreNotSymlink();

    try {
        Path globalLockPath = nixDBPath + "/big-lock";
        globalLock = openLockFile(globalLockPath.c_str(), true);
    } catch (SysError & e) {
        if (e.errNo != EACCES) throw;
        readOnlyMode = true;
        return;
    }
    
    if (!lockFile(globalLock, ltRead, false)) {
        printMsg(lvlError, "waiting for the big Nix store lock...");
        lockFile(globalLock, ltRead, true);
    }

    createDirs(nixDBPath + "/info");
    createDirs(nixDBPath + "/referrer");

    int curSchema = getSchema();
    if (curSchema > nixSchemaVersion)
        throw Error(format("current Nix store schema is version %1%, but I only support %2%")
            % curSchema % nixSchemaVersion);
    if (curSchema == 0) { /* new store */
        curSchema = nixSchemaVersion; 
        writeFile(schemaPath, (format("%1%") % nixSchemaVersion).str());
    }
    if (curSchema == 1) throw Error("your Nix store is no longer supported");
    if (curSchema < nixSchemaVersion) upgradeStore12();
}


LocalStore::~LocalStore()
{
    try {
        flushDelayedUpdates();

        foreach (RunningSubstituters::iterator, i, runningSubstituters) {
            i->second.toBuf.reset();
            i->second.to.reset();
            i->second.pid.wait(true);
        }
                
    } catch (...) {
        ignoreException();
    }
}


int LocalStore::getSchema()
{
    int curSchema = 0;
    if (pathExists(schemaPath)) {
        string s = readFile(schemaPath);
        if (!string2Int(s, curSchema))
            throw Error(format("`%1%' is corrupt") % schemaPath);
    }
    return curSchema;
}


void canonicalisePathMetaData(const Path & path, bool recurse)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    /* Change ownership to the current uid.  If it's a symlink, use
       lchown if available, otherwise don't bother.  Wrong ownership
       of a symlink doesn't matter, since the owning user can't change
       the symlink and can't delete it because the directory is not
       writable.  The only exception is top-level paths in the Nix
       store (since that directory is group-writable for the Nix build
       users group); we check for this case below. */
    if (st.st_uid != geteuid()) {
#if HAVE_LCHOWN
        if (lchown(path.c_str(), geteuid(), (gid_t) -1) == -1)
#else
        if (!S_ISLNK(st.st_mode) &&
            chown(path.c_str(), geteuid(), (gid_t) -1) == -1)
#endif
            throw SysError(format("changing owner of `%1%' to %2%")
                % path % geteuid());
    }
    
    if (!S_ISLNK(st.st_mode)) {

        /* Mask out all type related bits. */
        mode_t mode = st.st_mode & ~S_IFMT;
        
        if (mode != 0444 && mode != 0555) {
            mode = (st.st_mode & S_IFMT)
                 | 0444
                 | (st.st_mode & S_IXUSR ? 0111 : 0);
            if (chmod(path.c_str(), mode) == -1)
                throw SysError(format("changing mode of `%1%' to %2$o") % path % mode);
        }

        if (st.st_mtime != 0) {
            struct utimbuf utimbuf;
            utimbuf.actime = st.st_atime;
            utimbuf.modtime = 0;
            if (utime(path.c_str(), &utimbuf) == -1) 
                throw SysError(format("changing modification time of `%1%'") % path);
        }

    }

    if (recurse && S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
	    canonicalisePathMetaData(path + "/" + *i, true);
    }
}


void canonicalisePathMetaData(const Path & path)
{
    canonicalisePathMetaData(path, true);

    /* On platforms that don't have lchown(), the top-level path can't
       be a symlink, since we can't change its ownership. */
    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    if (st.st_uid != geteuid()) {
        assert(S_ISLNK(st.st_mode));
        throw Error(format("wrong ownership of top-level store path `%1%'") % path);
    }
}


static Path infoFileFor(const Path & path)
{
    string baseName = baseNameOf(path);
    return (format("%1%/info/%2%") % nixDBPath % baseName).str();
}


static Path referrersFileFor(const Path & path)
{
    string baseName = baseNameOf(path);
    return (format("%1%/referrer/%2%") % nixDBPath % baseName).str();
}


static Path tmpFileForAtomicUpdate(const Path & path)
{
    return (format("%1%/.%2%.%3%") % dirOf(path) % getpid() % baseNameOf(path)).str();
}


static void appendReferrer(const Path & from, const Path & to, bool lock)
{
    Path referrersFile = referrersFileFor(from);
    
    PathLocks referrersLock;
    if (lock) {
        referrersLock.lockPaths(singleton<PathSet, Path>(referrersFile));
        referrersLock.setDeletion(true);
    }

    AutoCloseFD fd = open(referrersFile.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd == -1) throw SysError(format("opening file `%1%'") % referrersFile);
    
    string s = " " + to;
    writeFull(fd, (const unsigned char *) s.c_str(), s.size());
}


/* Atomically update the referrers file.  If `purge' is true, the set
   of referrers is set to `referrers'.  Otherwise, the current set of
   referrers is purged of invalid paths. */
void LocalStore::rewriteReferrers(const Path & path, bool purge, PathSet referrers)
{
    Path referrersFile = referrersFileFor(path);
    
    PathLocks referrersLock(singleton<PathSet, Path>(referrersFile));
    referrersLock.setDeletion(true);

    if (purge)
        /* queryReferrers() purges invalid paths, so that's all we
           need. */
        queryReferrers(path, referrers);

    Path tmpFile = tmpFileForAtomicUpdate(referrersFile);
    
    AutoCloseFD fd = open(tmpFile.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (fd == -1) throw SysError(format("opening file `%1%'") % referrersFile);
    
    string s;
    foreach (PathSet::const_iterator, i, referrers) {
        s += " "; s += *i;
    }
    
    writeFull(fd, (const unsigned char *) s.c_str(), s.size());

    fd.close(); /* for Windows; can't rename open file */

    if (rename(tmpFile.c_str(), referrersFile.c_str()) == -1)
        throw SysError(format("cannot rename `%1%' to `%2%'") % tmpFile % referrersFile);
}


void LocalStore::flushDelayedUpdates()
{
    foreach (PathSet::iterator, i, delayedUpdates) {
        rewriteReferrers(*i, true, PathSet());
    }
    delayedUpdates.clear();
}


void LocalStore::registerValidPath(const Path & path,
    const Hash & hash, const PathSet & references,
    const Path & deriver)
{
    ValidPathInfo info;
    info.path = path;
    info.hash = hash;
    info.references = references;
    info.deriver = deriver;
    registerValidPath(info);
}


void LocalStore::registerValidPath(const ValidPathInfo & info, bool ignoreValidity)
{
    Path infoFile = infoFileFor(info.path);

    ValidPathInfo oldInfo;
    if (pathExists(infoFile)) oldInfo = queryPathInfo(info.path);

    /* Note that it's possible for infoFile to already exist. */

    /* Acquire a lock on each referrer file.  This prevents those
       paths from being invalidated.  (It would be a violation of the
       store invariants if we registered info.path as valid while some
       of its references are invalid.)  NB: there can be no deadlock
       here since we're acquiring the locks in sorted order. */
    PathSet lockNames;
    foreach (PathSet::const_iterator, i, info.references)
        if (*i != info.path) lockNames.insert(referrersFileFor(*i));
    PathLocks referrerLocks(lockNames);
    referrerLocks.setDeletion(true);
        
    string refs;
    foreach (PathSet::const_iterator, i, info.references) {
        if (!refs.empty()) refs += " ";
        refs += *i;

        if (!ignoreValidity && *i != info.path && !isValidPath(*i))
            throw Error(format("cannot register `%1%' as valid, because its reference `%2%' isn't valid")
                % info.path % *i);

        /* Update the referrer mapping for *i.  This must be done
           before the info file is written to maintain the invariant
           that if `path' is a valid path, then all its references
           have referrer mappings back to `path'.  A " " is prefixed
           to separate it from the previous entry.  It's not suffixed
           to deal with interrupted partial writes to this file. */
        if (oldInfo.references.find(*i) == oldInfo.references.end())
            appendReferrer(*i, info.path, false);
    }

    assert(info.hash.type == htSHA256);

    string s = (format(
        "Hash: sha256:%1%\n"
        "References: %2%\n"
        "Deriver: %3%\n"
        "Registered-At: %4%\n")
        % printHash(info.hash) % refs % info.deriver %
        (oldInfo.registrationTime ? oldInfo.registrationTime : time(0))).str();

    /* Atomically rewrite the info file. */
    Path tmpFile = tmpFileForAtomicUpdate(infoFile);
    writeFile(tmpFile, s);
    if (rename(tmpFile.c_str(), infoFile.c_str()) == -1)
        throw SysError(format("cannot rename `%1%' to `%2%'") % tmpFile % infoFile);

    pathInfoCache[info.path] = info;
}


Hash parseHashField(const Path & path, const string & s)
{
    string::size_type colon = s.find(':');
    if (colon == string::npos)
        throw Error(format("corrupt hash `%1%' in valid-path entry for `%2%'")
            % s % path);
    HashType ht = parseHashType(string(s, 0, colon));
    if (ht == htUnknown)
        throw Error(format("unknown hash type `%1%' in valid-path entry for `%2%'")
            % string(s, 0, colon) % path);
    return parseHash(ht, string(s, colon + 1));
}


ValidPathInfo LocalStore::queryPathInfo(const Path & path, bool ignoreErrors)
{
    ValidPathInfo res;
    res.path = path;

    assertStorePath(path);

    std::map<Path, ValidPathInfo>::iterator lookup = pathInfoCache.find(path);
    if (lookup != pathInfoCache.end()) return lookup->second;
    
    /* Read the info file. */
    Path infoFile = infoFileFor(path);
    if (!pathExists(infoFile))
        throw Error(format("path `%1%' is not valid") % path);
    string info = readFile(infoFile);

    /* Parse it. */
    Strings lines = tokenizeString(info, "\n");

    for (Strings::iterator i = lines.begin(); i != lines.end(); ++i) {
        unsigned int p = i->find(':');
        if (p == string::npos) continue; /* bad line */
        string name(*i, 0, p);
        string value(*i, p + 2);
        if (name == "References") {
            Strings refs = tokenizeString(value, " ");
            res.references = PathSet(refs.begin(), refs.end());
        } else if (name == "Deriver") {
            res.deriver = value;
        } else if (name == "Hash") {
            try {
                res.hash = parseHashField(path, value);
            } catch (Error & e) {
                if (!ignoreErrors) throw;
                printMsg(lvlError, format("cannot parse hash field in `%1%': %2%") % infoFile % e.msg());
            }
        } else if (name == "Registered-At") {
            int n = 0;
            string2Int(value, n);
            res.registrationTime = n;
        }
    }

    return pathInfoCache[path] = res;
}


bool LocalStore::isValidPath(const Path & path)
{
    /* Files in the info directory starting with a `.' are temporary
       files. */
    if (baseNameOf(path).at(0) == '.') return false;
    return pathExists(infoFileFor(path));
}


PathSet LocalStore::queryValidPaths()
{
    PathSet paths;
    Strings entries = readDirectory(nixDBPath + "/info");
    for (Strings::iterator i = entries.begin(); i != entries.end(); ++i)
        if (i->at(0) != '.') paths.insert(nixStore + "/" + *i);
    return paths;
}


void LocalStore::queryReferences(const Path & path,
    PathSet & references)
{
    ValidPathInfo info = queryPathInfo(path);
    references.insert(info.references.begin(), info.references.end());
}


bool LocalStore::queryReferrersInternal(const Path & path, PathSet & referrers)
{
    bool allValid = true;
    
    if (!isValidPath(path))
        throw Error(format("path `%1%' is not valid") % path);

    /* No locking is necessary here: updates are only done by
       appending or by atomically replacing the file.  When appending,
       there is a possibility that we see a partial entry, but it will
       just be filtered out below (the partially written path will not
       be valid, so it will be ignored). */

    Path referrersFile = referrersFileFor(path);
    if (!pathExists(referrersFile)) return true;
    
    AutoCloseFD fd = open(referrersFile.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(format("opening file `%1%'") % referrersFile);

    Paths refs = tokenizeString(readFile(fd), " ");

    for (Paths::iterator i = refs.begin(); i != refs.end(); ++i)
        /* Referrers can be invalid (see registerValidPath() for the
           invariant), so we only return one if it is valid. */
        if (isStorePath(*i) && isValidPath(*i)) referrers.insert(*i); else allValid = false;

    return allValid;
}


void LocalStore::queryReferrers(const Path & path, PathSet & referrers)
{
    queryReferrersInternal(path, referrers);
}


Path LocalStore::queryDeriver(const Path & path)
{
    return queryPathInfo(path).deriver;
}


void LocalStore::startSubstituter(const Path & substituter, RunningSubstituter & run)
{
    if (run.pid != -1) return;
    
    debug(format("starting substituter program `%1%'") % substituter);

    Pipe toPipe, fromPipe;
            
    toPipe.create();
    fromPipe.create();

    run.pid = fork();
            
    switch (run.pid) {

    case -1:
        throw SysError("unable to fork");

    case 0: /* child */
        try {
            fromPipe.readSide.close();
            toPipe.writeSide.close();
            if (dup2(toPipe.readSide, STDIN_FILENO) == -1)
                throw SysError("dupping stdin");
            if (dup2(fromPipe.writeSide, STDOUT_FILENO) == -1)
                throw SysError("dupping stdout");
            closeMostFDs(set<int>());
            execl(substituter.c_str(), substituter.c_str(), "--query", NULL);
            throw SysError(format("executing `%1%'") % substituter);
        } catch (std::exception & e) {
            std::cerr << "error: " << e.what() << std::endl;
        }
        quickExit(1);
    }

    /* Parent. */
    
    toPipe.readSide.close();
    fromPipe.writeSide.close();

    run.toBuf = boost::shared_ptr<stdio_filebuf>(new stdio_filebuf(toPipe.writeSide.borrow(), std::ios_base::out));
    run.to = boost::shared_ptr<std::ostream>(new std::ostream(&*run.toBuf));

    run.fromBuf = boost::shared_ptr<stdio_filebuf>(new stdio_filebuf(fromPipe.readSide.borrow(), std::ios_base::in));
    run.from = boost::shared_ptr<std::istream>(new std::istream(&*run.fromBuf));
}


template<class T> T getIntLine(std::istream & str)
{
    string s;
    T res;
    getline(str, s);
    if (!str || !string2Int(s, res)) throw Error("integer expected from stream");
    return res;
}


bool LocalStore::hasSubstitutes(const Path & path)
{
    foreach (Paths::iterator, i, substituters) {
        RunningSubstituter & run(runningSubstituters[*i]);
        startSubstituter(*i, run);

        *run.to << "have\n" << path << "\n" << std::flush;

        if (getIntLine<int>(*run.from)) return true;
    }

    return false;
}


bool LocalStore::querySubstitutablePathInfo(const Path & substituter,
    const Path & path, SubstitutablePathInfo & info)
{
    RunningSubstituter & run(runningSubstituters[substituter]);
    startSubstituter(substituter, run);

    *run.to << "info\n" << path << "\n" << std::flush;
        
    if (!getIntLine<int>(*run.from)) return false;
    
    getline(*run.from, info.deriver);
    if (info.deriver != "") assertStorePath(info.deriver);
    int nrRefs = getIntLine<int>(*run.from);
    while (nrRefs--) {
        Path p; getline(*run.from, p);
        assertStorePath(p);
        info.references.insert(p);
    }
    info.downloadSize = getIntLine<long long>(*run.from);
    
    return true;
}


bool LocalStore::querySubstitutablePathInfo(const Path & path,
    SubstitutablePathInfo & info)
{
    foreach (Paths::iterator, i, substituters)
        if (querySubstitutablePathInfo(*i, path, info)) return true;
    return false;
}


Hash LocalStore::queryPathHash(const Path & path)
{
    return queryPathInfo(path).hash;
}


static void dfsVisit(std::map<Path, ValidPathInfo> & infos,
    const Path & path, PathSet & visited, Paths & sorted)
{
    if (visited.find(path) != visited.end()) return;
    visited.insert(path);

    ValidPathInfo & info(infos[path]);
    
    for (PathSet::iterator i = info.references.begin();
         i != info.references.end(); ++i)
        if (infos.find(*i) != infos.end())
            dfsVisit(infos, *i, visited, sorted);

    sorted.push_back(path);
}


void LocalStore::registerValidPaths(const ValidPathInfos & infos)
{
    std::map<Path, ValidPathInfo> infosMap;
    
    /* Sort the paths topologically under the references relation, so
       that if path A is referenced by B, then A is registered before
       B. */
    for (ValidPathInfos::const_iterator i = infos.begin(); i != infos.end(); ++i)
        infosMap[i->path] = *i;

    PathSet visited;
    Paths sorted;
    for (ValidPathInfos::const_iterator i = infos.begin(); i != infos.end(); ++i)
        dfsVisit(infosMap, i->path, visited, sorted);

    for (Paths::iterator i = sorted.begin(); i != sorted.end(); ++i)
        registerValidPath(infosMap[*i]);
}


/* Invalidate a path.  The caller is responsible for checking that
   there are no referrers. */
void LocalStore::invalidatePath(const Path & path)
{
    debug(format("invalidating path `%1%'") % path);

    ValidPathInfo info;

    if (pathExists(infoFileFor(path))) {
        info = queryPathInfo(path);

        /* Remove the info file. */
        Path p = infoFileFor(path);
        if (unlink(p.c_str()) == -1)
            throw SysError(format("unlinking `%1%'") % p);
    }

    /* Remove the referrers file for `path'. */
    Path p = referrersFileFor(path);
    if (pathExists(p) && unlink(p.c_str()) == -1)
        throw SysError(format("unlinking `%1%'") % p);

    /* Clear `path' from the info cache. */
    pathInfoCache.erase(path);
    delayedUpdates.erase(path);

    /* Cause the referrer files for each path referenced by this one
       to be updated.  This has to happen after removing the info file
       to preserve the invariant (see registerValidPath()).

       The referrer files are updated lazily in flushDelayedUpdates()
       to prevent quadratic performance in the garbage collector
       (i.e., when N referrers to some path X are deleted, we have to
       rewrite the referrers file for X N times, causing O(N^2) I/O).

       What happens if we die before the referrer file can be updated?
       That's not a problem, because stale (invalid) entries in the
       referrer file are ignored by queryReferrers().  Thus a referrer
       file is allowed to have stale entries; removing them is just an
       optimisation.  verifyStore() gets rid of them eventually.
    */
    foreach (PathSet::iterator, i, info.references)
        if (*i != path) delayedUpdates.insert(*i);
}


Path LocalStore::addToStoreFromDump(const string & dump, const string & name,
    bool recursive, HashType hashAlgo)
{
    Hash h = hashString(hashAlgo, dump);

    Path dstPath = makeFixedOutputPath(recursive, hashAlgo, h, name);

    addTempRoot(dstPath);

    if (!isValidPath(dstPath)) {

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        PathLocks outputLock(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePathWrapped(dstPath);

            if (recursive) {
                StringSource source(dump);
                restorePath(dstPath, source);
            } else
                writeStringToFile(dstPath, dump);

            canonicalisePathMetaData(dstPath);

            /* Register the SHA-256 hash of the NAR serialisation of
               the path in the database.  We may just have computed it
               above (if called with recursive == true and hashAlgo ==
               sha256); otherwise, compute it here. */
            registerValidPath(dstPath,
                (recursive && hashAlgo == htSHA256) ? h :
                (recursive ? hashString(htSHA256, dump) : hashPath(htSHA256, dstPath)),
                PathSet(), "");
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


Path LocalStore::addToStore(const Path & _srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter)
{
    Path srcPath(absPath(_srcPath));
    debug(format("adding `%1%' to the store") % srcPath);

    /* Read the whole path into memory. This is not a very scalable
       method for very large paths, but `copyPath' is mainly used for
       small files. */
    StringSink sink;
    if (recursive) 
        dumpPath(srcPath, sink, filter);
    else
        sink.s = readFile(srcPath);

    return addToStoreFromDump(sink.s, baseNameOf(srcPath), recursive, hashAlgo);
}


Path LocalStore::addTextToStore(const string & name, const string & s,
    const PathSet & references)
{
    Path dstPath = computeStorePathForText(name, s, references);
    
    addTempRoot(dstPath);

    if (!isValidPath(dstPath)) {

        PathLocks outputLock(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePathWrapped(dstPath);

            writeStringToFile(dstPath, s);

            canonicalisePathMetaData(dstPath);
            
            registerValidPath(dstPath,
                hashPath(htSHA256, dstPath), references, "");
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


struct HashAndWriteSink : Sink
{
    Sink & writeSink;
    HashSink hashSink;
    bool hashing;
    HashAndWriteSink(Sink & writeSink) : writeSink(writeSink), hashSink(htSHA256)
    {
        hashing = true;
    }
    virtual void operator ()
        (const unsigned char * data, unsigned int len)
    {
        writeSink(data, len);
        if (hashing) hashSink(data, len);
    }
};


#define EXPORT_MAGIC 0x4558494e


static void checkSecrecy(const Path & path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0)
        throw Error(format("file `%1%' should be secret (inaccessible to everybody else)!") % path);
}


void LocalStore::exportPath(const Path & path, bool sign,
    Sink & sink)
{
    assertStorePath(path);

    addTempRoot(path);
    if (!isValidPath(path))
        throw Error(format("path `%1%' is not valid") % path);

    HashAndWriteSink hashAndWriteSink(sink);
    
    dumpPath(path, hashAndWriteSink);

    writeInt(EXPORT_MAGIC, hashAndWriteSink);

    writeString(path, hashAndWriteSink);
    
    PathSet references;
    queryReferences(path, references);
    writeStringSet(references, hashAndWriteSink);

    Path deriver = queryDeriver(path);
    writeString(deriver, hashAndWriteSink);

    if (sign) {
        Hash hash = hashAndWriteSink.hashSink.finish();
        hashAndWriteSink.hashing = false;

        writeInt(1, hashAndWriteSink);
        
        Path tmpDir = createTempDir();
        AutoDelete delTmp(tmpDir);
        Path hashFile = tmpDir + "/hash";
        writeStringToFile(hashFile, printHash(hash));

        Path secretKey = nixConfDir + "/signing-key.sec";
        checkSecrecy(secretKey);

        Strings args;
        args.push_back("rsautl");
        args.push_back("-sign");
        args.push_back("-inkey");
        args.push_back(secretKey);
        args.push_back("-in");
        args.push_back(hashFile);
        string signature = runProgram(OPENSSL_PATH, true, args);

        writeString(signature, hashAndWriteSink);
        
    } else
        writeInt(0, hashAndWriteSink);
}


struct HashAndReadSource : Source
{
    Source & readSource;
    HashSink hashSink;
    bool hashing;
    HashAndReadSource(Source & readSource) : readSource(readSource), hashSink(htSHA256)
    {
        hashing = true;
    }
    virtual void operator ()
        (unsigned char * data, unsigned int len)
    {
        readSource(data, len);
        if (hashing) hashSink(data, len);
    }
};


Path LocalStore::importPath(bool requireSignature, Source & source)
{
    HashAndReadSource hashAndReadSource(source);
    
    /* We don't yet know what store path this archive contains (the
       store path follows the archive data proper), and besides, we
       don't know yet whether the signature is valid. */
    Path tmpDir = createTempDir(nixStore);
    AutoDelete delTmp(tmpDir); /* !!! could be GC'ed! */
    Path unpacked = tmpDir + "/unpacked";

    restorePath(unpacked, hashAndReadSource);

    unsigned int magic = readInt(hashAndReadSource);
    if (magic != EXPORT_MAGIC)
        throw Error("Nix archive cannot be imported; wrong format");

    Path dstPath = readStorePath(hashAndReadSource);

    PathSet references = readStorePaths(hashAndReadSource);

    Path deriver = readString(hashAndReadSource);
    if (deriver != "") assertStorePath(deriver);

    Hash hash = hashAndReadSource.hashSink.finish();
    hashAndReadSource.hashing = false;

    bool haveSignature = readInt(hashAndReadSource) == 1;

    if (requireSignature && !haveSignature)
        throw Error("imported archive lacks a signature");
    
    if (haveSignature) {
        string signature = readString(hashAndReadSource);

        if (requireSignature) {
            Path sigFile = tmpDir + "/sig";
            writeStringToFile(sigFile, signature);

            Strings args;
            args.push_back("rsautl");
            args.push_back("-verify");
            args.push_back("-inkey");
            args.push_back(nixConfDir + "/signing-key.pub");
            args.push_back("-pubin");
            args.push_back("-in");
            args.push_back(sigFile);
            string hash2 = runProgram(OPENSSL_PATH, true, args);

            /* Note: runProgram() throws an exception if the signature
               is invalid. */

            if (printHash(hash) != hash2)
                throw Error(
                    "signed hash doesn't match actual contents of imported "
                    "archive; archive could be corrupt, or someone is trying "
                    "to import a Trojan horse");
        }
    }

    /* Do the actual import. */

    /* !!! way too much code duplication with addTextToStore() etc. */
    addTempRoot(dstPath);

    if (!isValidPath(dstPath)) {

        PathLocks outputLock;

        /* Lock the output path.  But don't lock if we're being called
           from a build hook (whose parent process already acquired a
           lock on this path). */
        Strings locksHeld = tokenizeString(getEnv("NIX_HELD_LOCKS"));
        if (find(locksHeld.begin(), locksHeld.end(), dstPath) == locksHeld.end())
            outputLock.lockPaths(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePathWrapped(dstPath);

            if (rename(unpacked.c_str(), dstPath.c_str()) == -1)
                throw SysError(format("cannot move `%1%' to `%2%'")
                    % unpacked % dstPath);

            canonicalisePathMetaData(dstPath);
            
            /* !!! if we were clever, we could prevent the hashPath()
               here. */
            if (deriver != "" && !isValidPath(deriver)) deriver = "";
            registerValidPath(dstPath,
                hashPath(htSHA256, dstPath), references, deriver);
        }
        
        outputLock.setDeletion(true);
    }
    
    return dstPath;
}


void LocalStore::deleteFromStore(const Path & path, unsigned long long & bytesFreed,
    unsigned long long & blocksFreed)
{
    bytesFreed = 0;

    assertStorePath(path);

    if (isValidPath(path)) {
        /* Acquire a lock on the referrers file to prevent new
           referrers to this path from appearing while we're deleting
           it. */
        PathLocks referrersLock(singleton<PathSet, Path>(referrersFileFor(path)));
        referrersLock.setDeletion(true);
        PathSet referrers; queryReferrers(path, referrers);
        referrers.erase(path); /* ignore self-references */
        if (!referrers.empty())
            throw PathInUse(format("cannot delete path `%1%' because it is in use by `%2%'")
                % path % showPaths(referrers));
        invalidatePath(path);
    }

    deletePathWrapped(path, bytesFreed, blocksFreed);
}


void LocalStore::verifyStore(bool checkContents)
{
    /* Check whether all valid paths actually exist. */
    printMsg(lvlInfo, "checking path existence");

    PathSet validPaths2 = queryValidPaths(), validPaths;
    
    for (PathSet::iterator i = validPaths2.begin(); i != validPaths2.end(); ++i) {
        checkInterrupt();
        if (!isStorePath(*i)) {
            printMsg(lvlError, format("path `%1%' is not in the Nix store") % *i);
            invalidatePath(*i);
        } else if (!pathExists(*i)) {
            printMsg(lvlError, format("path `%1%' disappeared") % *i);
            invalidatePath(*i);
        } else
            validPaths.insert(*i);
    }


    /* Check the store path meta-information. */
    printMsg(lvlInfo, "checking path meta-information");

    std::map<Path, PathSet> referrersCache;
    
    for (PathSet::iterator i = validPaths.begin(); i != validPaths.end(); ++i) {
        bool update = false;
        ValidPathInfo info = queryPathInfo(*i, true);

        /* Check the references: each reference should be valid, and
           it should have a matching referrer. */
        for (PathSet::iterator j = info.references.begin();
             j != info.references.end(); ++j)
        {
            if (referrersCache.find(*j) == referrersCache.end())
                queryReferrers(*j, referrersCache[*j]);
            if (referrersCache[*j].find(*i) == referrersCache[*j].end()) {
                printMsg(lvlError, format("adding missing referrer mapping from `%1%' to `%2%'")
                    % *j % *i);
                appendReferrer(*j, *i, true);
            }
            if (validPaths.find(*j) == validPaths.end()) {
                printMsg(lvlError, format("incomplete closure: `%1%' needs missing `%2%'")
                    % *i % *j);
                /* nothing we can do about it... */
            }
        }

        /* Check the deriver.  (Note that the deriver doesn't have to
           be a valid path.) */
        if (!info.deriver.empty() && !isStorePath(info.deriver)) {
            info.deriver = "";
            update = true;
        }

        /* Check the content hash (optionally - slow). */
        if (info.hash.hashSize == 0) {
            printMsg(lvlError, format("re-hashing `%1%'") % *i);
            info.hash = hashPath(htSHA256, *i);
            update = true;
        } else if (checkContents) {
            debug(format("checking contents of `%1%'") % *i);
            Hash current = hashPath(info.hash.type, *i);
            if (current != info.hash) {
                printMsg(lvlError, format("path `%1%' was modified! "
                        "expected hash `%2%', got `%3%'")
                    % *i % printHash(info.hash) % printHash(current));
            }
        }

        if (update) registerValidPath(info);
    }

    referrersCache.clear();
    

    /* Check the referrers. */
    printMsg(lvlInfo, "checking referrers");

    std::map<Path, PathSet> referencesCache;
    
    Strings entries = readDirectory(nixDBPath + "/referrer");
    for (Strings::iterator i = entries.begin(); i != entries.end(); ++i) {
        Path from = nixStore + "/" + *i;
        
        if (validPaths.find(from) == validPaths.end()) {
            /* !!! This removes lock files as well.  Need to check
               whether that's okay. */
            printMsg(lvlError, format("removing referrers file for invalid `%1%'") % from);
            Path p = referrersFileFor(from);
            if (unlink(p.c_str()) == -1)
                throw SysError(format("unlinking `%1%'") % p);
            continue;
        }

        PathSet referrers;
        bool allValid = queryReferrersInternal(from, referrers);
        bool update = false;

        if (!allValid) {
            printMsg(lvlError, format("removing some stale referrers for `%1%'") % from);
            update = true;
        }

        /* Each referrer should have a matching reference. */
        PathSet referrersNew;
        for (PathSet::iterator j = referrers.begin(); j != referrers.end(); ++j) {
            if (referencesCache.find(*j) == referencesCache.end())
                queryReferences(*j, referencesCache[*j]);
            if (referencesCache[*j].find(from) == referencesCache[*j].end()) {
                printMsg(lvlError, format("removing unexpected referrer mapping from `%1%' to `%2%'")
                    % from % *j);
                update = true;
            } else referrersNew.insert(*j);
        }

        if (update) rewriteReferrers(from, false, referrersNew);
    }
}


}
