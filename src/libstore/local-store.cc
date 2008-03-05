#include "config.h"
#include "local-store.hh"
#include "util.hh"
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
#include <fcntl.h> // !!! remove


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
    
    if (readOnlyMode) return;

    checkStoreNotSymlink();

    try {
        createDirs(nixDBPath + "/info");
        createDirs(nixDBPath + "/referrer");
    } catch (Error & e) {
        // !!! fix access check
        printMsg(lvlTalkative, "cannot access Nix database; continuing anyway");
        readOnlyMode = true;
        return;
    }
    
    int curSchema = 0;
    Path schemaFN = nixDBPath + "/schema";
    if (pathExists(schemaFN)) {
        string s = readFile(schemaFN);
        if (!string2Int(s, curSchema))
            throw Error(format("`%1%' is corrupt") % schemaFN);
    }

    if (curSchema > nixSchemaVersion)
        throw Error(format("current Nix store schema is version %1%, but I only support %2%")
            % curSchema % nixSchemaVersion);

    if (curSchema < nixSchemaVersion) {
        if (curSchema == 0) /* new store */
            curSchema = nixSchemaVersion;
        if (curSchema <= 1)
            throw Error("your Nix store is no longer supported");
        if (curSchema <= 4) upgradeStore12();
        writeFile(schemaFN, (format("%1%") % nixSchemaVersion).str());
    }
}


LocalStore::~LocalStore()
{
}


void copyPath(const Path & src, const Path & dst, PathFilter & filter)
{
    debug(format("copying `%1%' to `%2%'") % src % dst);

    /* Dump an archive of the path `src' into a string buffer, then
       restore the archive to `dst'.  This is not a very good method
       for very large paths, but `copyPath' is mainly used for small
       files. */ 

    StringSink sink;
    dumpPath(src, sink, filter);

    StringSource source(sink.s);
    restorePath(dst, source);
}


static void _canonicalisePathMetaData(const Path & path, bool recurse)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    /* Change ownership to the current uid.  If its a symlink, use
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
	    _canonicalisePathMetaData(path + "/" + *i, true);
    }
}


void canonicalisePathMetaData(const Path & path)
{
    _canonicalisePathMetaData(path, true);

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


Path infoFileFor(const Path & path)
{
    string baseName = baseNameOf(path);
    return (format("%1%/info/%2%") % nixDBPath % baseName).str();
}


Path referrersFileFor(const Path & path)
{
    string baseName = baseNameOf(path);
    return (format("%1%/referrer/%2%") % nixDBPath % baseName).str();
}


/* !!! move to util.cc */
void appendFile(const Path & path, const string & s)
{
    AutoCloseFD fd = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd == -1)
        throw SysError(format("opening file `%1%'") % path);
    writeFull(fd, (unsigned char *) s.c_str(), s.size());
}


static void appendReferrer(const Path & from, const Path & to)
{
    Path referrersFile = referrersFileFor(from);
    /* !!! locking */
    appendFile(referrersFile, " " + to);
}


/* Atomically update the referrers file. */
static void rewriteReferrers(const Path & path, const PathSet & referrers)
{
    string s;
    for (PathSet::const_iterator i = referrers.begin(); i != referrers.end(); ++i) {
        s += " "; s += *i;
    }
    writeFile(referrersFileFor(path), s); /* !!! atomicity, locking */
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


void LocalStore::registerValidPath(const ValidPathInfo & info)
{
    Path infoFile = infoFileFor(info.path);
    if (pathExists(infoFile)) return;

    // !!! acquire PathLock on infoFile here?

    string refs;
    for (PathSet::const_iterator i = info.references.begin();
         i != info.references.end(); ++i)
    {
        if (!refs.empty()) refs += " ";
        refs += *i;

        /* Update the referrer mapping for *i.  This must be done
           before the info file is written to maintain the invariant
           that if `path' is a valid path, then all its references
           have referrer mappings back to `path'.  A " " is prefixed
           to separate it from the previous entry.  It's not suffixed
           to deal with interrupted partial writes to this file. */
        appendReferrer(*i, info.path);
    }

    string s = (format(
        "Hash: sha256:%1%\n"
        "References: %2%\n"
        "Deriver: %3%\n"
        "Registered-At: %4%\n")
        % printHash(info.hash) % refs % info.deriver % time(0)).str();

    // !!! atomicity
    writeFile(infoFile, s);
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


ValidPathInfo LocalStore::queryPathInfo(const Path & path)
{
    ValidPathInfo res;

    assertStorePath(path);

    std::map<Path, ValidPathInfo>::iterator lookup = pathInfoCache.find(path);
    if (lookup != pathInfoCache.end()) return lookup->second;
    
    //printMsg(lvlError, "queryPathInfo: " + path);
    
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
            res.hash = parseHashField(path, value);
        }
    }

    return pathInfoCache[path] = res;
}


bool LocalStore::isValidPath(const Path & path)
{
    return pathExists(infoFileFor(path));
}


PathSet LocalStore::queryValidPaths()
{
    PathSet paths;
    Strings entries = readDirectory(nixDBPath + "/info");
    for (Strings::iterator i = entries.begin(); i != entries.end(); ++i) 
        paths.insert(nixStore + "/" + *i);
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

    Path p = referrersFileFor(path);
    if (!pathExists(p)) return true;
    Paths refs = tokenizeString(readFile(p), " ");

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


PathSet LocalStore::querySubstitutablePaths()
{
    if (!substitutablePathsLoaded) {
        for (Paths::iterator i = substituters.begin(); i != substituters.end(); ++i) {
            debug(format("running `%1%' to find out substitutable paths") % *i);
            Strings args;
            args.push_back("--query-paths");
            Strings ss = tokenizeString(runProgram(*i, false, args), "\n");
            for (Strings::iterator j = ss.begin(); j != ss.end(); ++j) {
                if (!isStorePath(*j))
                    throw Error(format("`%1%' returned a bad substitutable path `%2%'")
                        % *i % *j);
                substitutablePaths.insert(*j);
            }
        }
        substitutablePathsLoaded = true;
    }

    return substitutablePaths;
}


bool LocalStore::hasSubstitutes(const Path & path)
{
    if (!substitutablePathsLoaded)
        querySubstitutablePaths(); 
    return substitutablePaths.find(path) != substitutablePaths.end();
}


Hash LocalStore::queryPathHash(const Path & path)
{
    return queryPathInfo(path).hash;
}


void LocalStore::registerValidPaths(const ValidPathInfos & infos)
{
    throw Error("!!! registerValidPaths");
#if 0
    PathSet newPaths;
    for (ValidPathInfos::const_iterator i = infos.begin();
         i != infos.end(); ++i)
        newPaths.insert(i->path);
        
    for (ValidPathInfos::const_iterator i = infos.begin();
         i != infos.end(); ++i)
    {
        assertStorePath(i->path);

        debug(format("registering path `%1%'") % i->path);
        oldSetHash(txn, i->path, i->hash);

        setReferences(txn, i->path, i->references);
    
        /* Check that all referenced paths are also valid (or about to
           become valid). */
        for (PathSet::iterator j = i->references.begin();
             j != i->references.end(); ++j)
            if (!isValidPathTxn(txn, *j) && newPaths.find(*j) == newPaths.end())
                throw Error(format("cannot register path `%1%' as valid, since its reference `%2%' is invalid")
                    % i->path % *j);

        setDeriver(txn, i->path, i->deriver);
    }
#endif
}


/* Invalidate a path.  The caller is responsible for checking that
   there are no referrers. */
static void invalidatePath(const Path & path)
{
    debug(format("invalidating path `%1%'") % path);

    /* Remove the info file. */
    Path p = infoFileFor(path);
    if (unlink(p.c_str()) == -1)
        throw SysError(format("unlinking `%1%'") % p);

    /* Remove the corresponding referrer entries for each path
       referenced by this one.  This has to happen after removing the
       info file to preserve the invariant (see
       registerValidPath()). */
    /* !!! */
}


Path LocalStore::addToStore(const Path & _srcPath, bool fixed,
    bool recursive, string hashAlgo, PathFilter & filter)
{
    Path srcPath(absPath(_srcPath));
    debug(format("adding `%1%' to the store") % srcPath);

    std::pair<Path, Hash> pr =
        computeStorePathForPath(srcPath, fixed, recursive, hashAlgo, filter);
    Path & dstPath(pr.first);
    Hash & h(pr.second);

    addTempRoot(dstPath);

    if (!isValidPath(dstPath)) {

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        PathLocks outputLock(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePathWrapped(dstPath);

            copyPath(srcPath, dstPath, filter);

            Hash h2 = hashPath(htSHA256, dstPath, filter);
            if (h != h2)
                throw Error(format("contents of `%1%' changed while copying it to `%2%' (%3% -> %4%)")
                    % srcPath % dstPath % printHash(h) % printHash(h2));

            canonicalisePathMetaData(dstPath);
            
            registerValidPath(dstPath, h, PathSet(), "");
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


Path LocalStore::addTextToStore(const string & suffix, const string & s,
    const PathSet & references)
{
    Path dstPath = computeStorePathForText(suffix, s, references);
    
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
    AutoDelete delTmp(tmpDir);
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

        PathLocks outputLock(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePathWrapped(dstPath);

            if (rename(unpacked.c_str(), dstPath.c_str()) == -1)
                throw SysError(format("cannot move `%1%' to `%2%'")
                    % unpacked % dstPath);

            canonicalisePathMetaData(dstPath);
            
            /* !!! if we were clever, we could prevent the hashPath()
               here. */
            if (!isValidPath(deriver)) deriver = "";
            registerValidPath(dstPath,
                hashPath(htSHA256, dstPath), references, deriver);
        }
        
        outputLock.setDeletion(true);
    }
    
    return dstPath;
}


void LocalStore::deleteFromStore(const Path & _path, unsigned long long & bytesFreed)
{
    bytesFreed = 0;
    Path path(canonPath(_path));

    assertStorePath(path);

    if (isValidPath(path)) {
        PathSet referrers; queryReferrers(path, referrers);
        referrers.erase(path); /* ignore self-references */
        /* !!! check: can a new referrer appear now? */
        if (!referrers.empty())
            throw PathInUse(format("cannot delete path `%1%' because it is in use by `%2%'")
                % path % showPaths(referrers));
        invalidatePath(path);
    }

    deletePathWrapped(path, bytesFreed);
}


void LocalStore::verifyStore(bool checkContents)
{
    /* !!! acquire the GC lock or something? */


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
        ValidPathInfo info = queryPathInfo(*i);

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
                appendReferrer(*j, *i);
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
        if (checkContents) {
            debug(format("checking contents of `%1%'") % *i);
            Hash current = hashPath(info.hash.type, *i);
            if (current != info.hash) {
                printMsg(lvlError, format("path `%1%' was modified! "
                        "expected hash `%2%', got `%3%'")
                    % *i % printHash(info.hash) % printHash(current));
            }
        }

        if (update)
            /* !!! */;
    }

    referrersCache.clear();
    

    /* Check the referrers. */
    printMsg(lvlInfo, "checking referrers");

    std::map<Path, PathSet> referencesCache;
    
    Strings entries = readDirectory(nixDBPath + "/referrer");
    for (Strings::iterator i = entries.begin(); i != entries.end(); ++i) {
        Path from = nixStore + "/" + *i;
        
        if (validPaths.find(from) == validPaths.end()) {
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

        if (update) rewriteReferrers(from, referrersNew);
    }
}


typedef std::map<Hash, std::pair<Path, ino_t> > HashToPath;


static void makeWritable(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);
    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
        throw SysError(format("changing writability of `%1%'") % path);
}


static void hashAndLink(bool dryRun, HashToPath & hashToPath,
    OptimiseStats & stats, const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    /* Sometimes SNAFUs can cause files in the Nix store to be
       modified, in particular when running programs as root under
       NixOS (example: $fontconfig/var/cache being modified).  Skip
       those files. */
    if (S_ISREG(st.st_mode) && (st.st_mode & S_IWUSR)) {
        printMsg(lvlError, format("skipping suspicious writable file `%1%'") % path);
        return;
    }

    /* We can hard link regular files and symlinks. */
    if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {

        /* Hash the file.  Note that hashPath() returns the hash over
           the NAR serialisation, which includes the execute bit on
           the file.  Thus, executable and non-executable files with
           the same contents *won't* be linked (which is good because
           otherwise the permissions would be screwed up).

           Also note that if `path' is a symlink, then we're hashing
           the contents of the symlink (i.e. the result of
           readlink()), not the contents of the target (which may not
           even exist). */
        Hash hash = hashPath(htSHA256, path);
        stats.totalFiles++;
        printMsg(lvlDebug, format("`%1%' has hash `%2%'") % path % printHash(hash));

        std::pair<Path, ino_t> prevPath = hashToPath[hash];
        
        if (prevPath.first == "") {
            hashToPath[hash] = std::pair<Path, ino_t>(path, st.st_ino);
            return;
        }
            
        /* Yes!  We've seen a file with the same contents.  Replace
           the current file with a hard link to that file. */
        stats.sameContents++;
        if (prevPath.second == st.st_ino) {
            printMsg(lvlDebug, format("`%1%' is already linked to `%2%'") % path % prevPath.first);
            return;
        }
        
        if (!dryRun) {
            
            printMsg(lvlTalkative, format("linking `%1%' to `%2%'") % path % prevPath.first);

            Path tempLink = (format("%1%.tmp-%2%-%3%")
                % path % getpid() % rand()).str();

            /* Make the containing directory writable, but only if
               it's not the store itself (we don't want or need to
               mess with  its permissions). */
            bool mustToggle = !isStorePath(path);
            if (mustToggle) makeWritable(dirOf(path));
        
            if (link(prevPath.first.c_str(), tempLink.c_str()) == -1)
                throw SysError(format("cannot link `%1%' to `%2%'")
                    % tempLink % prevPath.first);

            /* Atomically replace the old file with the new hard link. */
            if (rename(tempLink.c_str(), path.c_str()) == -1)
                throw SysError(format("cannot rename `%1%' to `%2%'")
                    % tempLink % path);

            /* Make the directory read-only again and reset its
               timestamp back to 0. */
            if (mustToggle) _canonicalisePathMetaData(dirOf(path), false);
            
        } else
            printMsg(lvlTalkative, format("would link `%1%' to `%2%'") % path % prevPath.first);
        
        stats.filesLinked++;
        stats.bytesFreed += st.st_size;
    }

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
	    hashAndLink(dryRun, hashToPath, stats, path + "/" + *i);
    }
}


void LocalStore::optimiseStore(bool dryRun, OptimiseStats & stats)
{
    HashToPath hashToPath;

    PathSet paths = queryValidPaths();

    for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
        addTempRoot(*i);
        if (!isValidPath(*i)) continue; /* path was GC'ed, probably */
        startNest(nest, lvlChatty, format("hashing files in `%1%'") % *i);
        hashAndLink(dryRun, hashToPath, stats, *i);
    }
}


}
