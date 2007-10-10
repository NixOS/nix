#include "config.h"
#include "local-store.hh"
#include "util.hh"
#include "globals.hh"
#include "db.hh"
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


namespace nix {

    
/* Nix database. */
static Database nixDB;


/* Database tables. */

/* dbValidPaths :: Path -> ()

   The existence of a key $p$ indicates that path $p$ is valid (that
   is, produced by a succesful build). */
static TableId dbValidPaths = 0;

/* dbReferences :: Path -> [Path]

   This table lists the outgoing file system references for each
   output path that has been built by a Nix derivation.  These are
   found by scanning the path for the hash components of input
   paths. */
static TableId dbReferences = 0;

/* dbReferrers :: Path -> Path

   This table is just the reverse mapping of dbReferences.  This table
   can have duplicate keys, each corresponding value denoting a single
   referrer. */
static TableId dbReferrers = 0;

/* dbDerivers :: Path -> [Path]

   This table lists the derivation used to build a path.  There can
   only be multiple such paths for fixed-output derivations (i.e.,
   derivations specifying an expected hash). */
static TableId dbDerivers = 0;


static void upgradeStore07();
static void upgradeStore09();
static void upgradeStore11();


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


LocalStore::LocalStore(bool reserveSpace)
{
    substitutablePathsLoaded = false;
    
    if (readOnlyMode) return;

    checkStoreNotSymlink();

    try {
        Path reservedPath = nixDBPath + "/reserved";
        string s = querySetting("gc-reserved-space", "");
        int reservedSize;
        if (!string2Int(s, reservedSize)) reservedSize = 1024 * 1024;
        if (reserveSpace) {
            struct stat st;
            if (stat(reservedPath.c_str(), &st) == -1 ||
                st.st_size != reservedSize)
                writeFile(reservedPath, string(reservedSize, 'X'));
        }
        else
            deletePath(reservedPath);
    } catch (SysError & e) { /* don't care about errors */
    }

    try {
        nixDB.open(nixDBPath);
    } catch (DbNoPermission & e) {
        printMsg(lvlTalkative, "cannot access Nix database; continuing anyway");
        readOnlyMode = true;
        return;
    }
    dbValidPaths = nixDB.openTable("validpaths");
    dbReferences = nixDB.openTable("references");
    dbReferrers = nixDB.openTable("referrers", true); /* must be sorted */
    dbDerivers = nixDB.openTable("derivers");

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
        if (curSchema <= 1)
            upgradeStore07();
        if (curSchema == 2)
            upgradeStore09();
        if (curSchema == 3)
            upgradeStore11();
        writeFile(schemaFN, (format("%1%") % nixSchemaVersion).str());
    }
}


LocalStore::~LocalStore()
{
    /* If the database isn't open, this is a NOP. */
    try {
        nixDB.close();
    } catch (...) {
        ignoreException();
    }
}


void createStoreTransaction(Transaction & txn)
{
    Transaction txn2(nixDB);
    txn2.moveTo(txn);
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


bool isValidPathTxn(const Transaction & txn, const Path & path)
{
    string s;
    return nixDB.queryString(txn, dbValidPaths, path, s);
}


bool LocalStore::isValidPath(const Path & path)
{
    return isValidPathTxn(noTxn, path);
}


static string addPrefix(const string & prefix, const string & s)
{
    return prefix + string(1, (char) 0) + s;
}


static string stripPrefix(const string & prefix, const string & s)
{
    if (s.size() <= prefix.size() ||
        string(s, 0, prefix.size()) != prefix ||
        s[prefix.size()] != 0)
        throw Error(format("string `%1%' is missing prefix `%2%'")
            % s % prefix);
    return string(s, prefix.size() + 1);
}


static PathSet getReferrers(const Transaction & txn, const Path & storePath)
{
    PathSet referrers;
    Strings keys;
    nixDB.enumTable(txn, dbReferrers, keys, storePath + string(1, (char) 0));
    for (Strings::iterator i = keys.begin(); i != keys.end(); ++i)
        referrers.insert(stripPrefix(storePath, *i));
    return referrers;
}


void setReferences(const Transaction & txn, const Path & storePath,
    const PathSet & references)
{
    /* For invalid paths, we can only clear the references. */
    if (references.size() > 0 && !isValidPathTxn(txn, storePath))
        throw Error(
            format("cannot set references for invalid path `%1%'") % storePath);

    Paths oldReferences;
    nixDB.queryStrings(txn, dbReferences, storePath, oldReferences);

    PathSet oldReferences2(oldReferences.begin(), oldReferences.end());
    if (oldReferences2 == references) return;
    
    nixDB.setStrings(txn, dbReferences, storePath,
        Paths(references.begin(), references.end()));

    /* Update the referrers mappings of all new referenced paths. */
    for (PathSet::const_iterator i = references.begin();
         i != references.end(); ++i)
        if (oldReferences2.find(*i) == oldReferences2.end())
            nixDB.setString(txn, dbReferrers, addPrefix(*i, storePath), "");

    /* Remove referrer mappings from paths that are no longer
       references. */
    for (Paths::iterator i = oldReferences.begin();
         i != oldReferences.end(); ++i)
        if (references.find(*i) == references.end())
            nixDB.delPair(txn, dbReferrers, addPrefix(*i, storePath));
}


void queryReferences(const Transaction & txn,
    const Path & storePath, PathSet & references)
{
    Paths references2;
    if (!isValidPathTxn(txn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    nixDB.queryStrings(txn, dbReferences, storePath, references2);
    references.insert(references2.begin(), references2.end());
}


void LocalStore::queryReferences(const Path & storePath,
    PathSet & references)
{
    nix::queryReferences(noTxn, storePath, references);
}


void queryReferrers(const Transaction & txn,
    const Path & storePath, PathSet & referrers)
{
    if (!isValidPathTxn(txn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    PathSet referrers2 = getReferrers(txn, storePath);
    referrers.insert(referrers2.begin(), referrers2.end());
}


void LocalStore::queryReferrers(const Path & storePath,
    PathSet & referrers)
{
    nix::queryReferrers(noTxn, storePath, referrers);
}


void setDeriver(const Transaction & txn, const Path & storePath,
    const Path & deriver)
{
    assertStorePath(storePath);
    if (deriver == "") return;
    assertStorePath(deriver);
    if (!isValidPathTxn(txn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    nixDB.setString(txn, dbDerivers, storePath, deriver);
}


static Path queryDeriver(const Transaction & txn, const Path & storePath)
{
    if (!isValidPathTxn(txn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    Path deriver;
    if (nixDB.queryString(txn, dbDerivers, storePath, deriver))
        return deriver;
    else
        return "";
}


Path LocalStore::queryDeriver(const Path & path)
{
    return nix::queryDeriver(noTxn, path);
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


static void setHash(const Transaction & txn, const Path & storePath,
    const Hash & hash)
{
    assert(hash.type == htSHA256);
    nixDB.setString(txn, dbValidPaths, storePath, "sha256:" + printHash(hash));
}


static Hash queryHash(const Transaction & txn, const Path & storePath)
{
    string s;
    nixDB.queryString(txn, dbValidPaths, storePath, s);
    string::size_type colon = s.find(':');
    if (colon == string::npos)
        throw Error(format("corrupt hash `%1%' in valid-path entry for `%2%'")
            % s % storePath);
    HashType ht = parseHashType(string(s, 0, colon));
    if (ht == htUnknown)
        throw Error(format("unknown hash type `%1%' in valid-path entry for `%2%'")
            % string(s, 0, colon) % storePath);
    return parseHash(ht, string(s, colon + 1));
}


Hash LocalStore::queryPathHash(const Path & path)
{
    if (!isValidPath(path))
        throw Error(format("path `%1%' is not valid") % path);
    return queryHash(noTxn, path);
}


void registerValidPath(const Transaction & txn,
    const Path & path, const Hash & hash, const PathSet & references,
    const Path & deriver)
{
    ValidPathInfo info;
    info.path = path;
    info.hash = hash;
    info.references = references;
    info.deriver = deriver;
    ValidPathInfos infos;
    infos.push_back(info);
    registerValidPaths(txn, infos);
}


void registerValidPaths(const Transaction & txn,
    const ValidPathInfos & infos)
{
    PathSet newPaths;
    for (ValidPathInfos::const_iterator i = infos.begin();
         i != infos.end(); ++i)
        newPaths.insert(i->path);
        
    for (ValidPathInfos::const_iterator i = infos.begin();
         i != infos.end(); ++i)
    {
        assertStorePath(i->path);

        debug(format("registering path `%1%'") % i->path);
        setHash(txn, i->path, i->hash);

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
}


/* Invalidate a path.  The caller is responsible for checking that
   there are no referrers. */
static void invalidatePath(Transaction & txn, const Path & path)
{
    debug(format("invalidating path `%1%'") % path);

    /* Clear the `references' entry for this path, as well as the
       inverse `referrers' entries, and the `derivers' entry. */
    setReferences(txn, path, PathSet());
    nixDB.delPair(txn, dbDerivers, path);
    nixDB.delPair(txn, dbValidPaths, path);
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
            
            Transaction txn(nixDB);
            registerValidPath(txn, dstPath, h, PathSet(), "");
            txn.commit();
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
            
            Transaction txn(nixDB);
            registerValidPath(txn, dstPath,
                hashPath(htSHA256, dstPath), references, "");
            txn.commit();
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

    /* Wrap all of this in a transaction to make sure that we export
       consistent metadata. */
    Transaction txn(nixDB);
    addTempRoot(path);
    if (!isValidPathTxn(txn, path))
        throw Error(format("path `%1%' is not valid") % path);

    HashAndWriteSink hashAndWriteSink(sink);
    
    dumpPath(path, hashAndWriteSink);

    writeInt(EXPORT_MAGIC, hashAndWriteSink);

    writeString(path, hashAndWriteSink);
    
    PathSet references;
    nix::queryReferences(txn, path, references);
    writeStringSet(references, hashAndWriteSink);

    Path deriver = nix::queryDeriver(txn, path);
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

    txn.commit();
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
            
            Transaction txn(nixDB);
            /* !!! if we were clever, we could prevent the hashPath()
               here. */
            if (!isValidPath(deriver)) deriver = "";
            registerValidPath(txn, dstPath,
                hashPath(htSHA256, dstPath), references, deriver);
            txn.commit();
        }
        
        outputLock.setDeletion(true);
    }
    
    return dstPath;
}


void deleteFromStore(const Path & _path, unsigned long long & bytesFreed)
{
    bytesFreed = 0;
    Path path(canonPath(_path));

    assertStorePath(path);

    Transaction txn(nixDB);
    if (isValidPathTxn(txn, path)) {
        PathSet referrers = getReferrers(txn, path);
        for (PathSet::iterator i = referrers.begin();
             i != referrers.end(); ++i)
            if (*i != path && isValidPathTxn(txn, *i))
                throw PathInUse(format("cannot delete path `%1%' because it is in use by path `%2%'") % path % *i);
        invalidatePath(txn, path);
    }
    txn.commit();

    deletePathWrapped(path, bytesFreed);
}


void verifyStore(bool checkContents)
{
    Transaction txn(nixDB);

    
    printMsg(lvlInfo, "checking path existence");

    Paths paths;
    PathSet validPaths;
    nixDB.enumTable(txn, dbValidPaths, paths);

    for (Paths::iterator i = paths.begin(); i != paths.end(); ++i) {
        checkInterrupt();
        if (!pathExists(*i)) {
            printMsg(lvlError, format("path `%1%' disappeared") % *i);
            invalidatePath(txn, *i);
        } else if (!isStorePath(*i)) {
            printMsg(lvlError, format("path `%1%' is not in the Nix store") % *i);
            invalidatePath(txn, *i);
        } else {
            if (checkContents) {
                debug(format("checking contents of `%1%'") % *i);
                Hash expected = queryHash(txn, *i);
                Hash current = hashPath(expected.type, *i);
                if (current != expected) {
                    printMsg(lvlError, format("path `%1%' was modified! "
                                 "expected hash `%2%', got `%3%'")
                        % *i % printHash(expected) % printHash(current));
                }
            }
            validPaths.insert(*i);
        }
    }


    /* Check the cleanup invariant: only valid paths can have
       `references', `referrers', or `derivers' entries. */


    /* Check the `derivers' table. */
    printMsg(lvlInfo, "checking the derivers table");
    Paths deriversKeys;
    nixDB.enumTable(txn, dbDerivers, deriversKeys);
    for (Paths::iterator i = deriversKeys.begin();
         i != deriversKeys.end(); ++i)
    {
        if (validPaths.find(*i) == validPaths.end()) {
            printMsg(lvlError, format("removing deriver entry for invalid path `%1%'")
                % *i);
            nixDB.delPair(txn, dbDerivers, *i);
        }
        else {
            Path deriver = queryDeriver(txn, *i);
            if (!isStorePath(deriver)) {
                printMsg(lvlError, format("removing corrupt deriver `%1%' for `%2%'")
                    % deriver % *i);
                nixDB.delPair(txn, dbDerivers, *i);
            }
        }
    }


    /* Check the `references' table. */
    printMsg(lvlInfo, "checking the references table");
    Paths referencesKeys;
    nixDB.enumTable(txn, dbReferences, referencesKeys);
    for (Paths::iterator i = referencesKeys.begin();
         i != referencesKeys.end(); ++i)
    {
        if (validPaths.find(*i) == validPaths.end()) {
            printMsg(lvlError, format("removing references entry for invalid path `%1%'")
                % *i);
            setReferences(txn, *i, PathSet());
        }
        else {
            PathSet references;
            queryReferences(txn, *i, references);
            for (PathSet::iterator j = references.begin();
                 j != references.end(); ++j)
            {
                string dummy;
                if (!nixDB.queryString(txn, dbReferrers, addPrefix(*j, *i), dummy)) {
                    printMsg(lvlError, format("adding missing referrer mapping from `%1%' to `%2%'")
                        % *j % *i);
                    nixDB.setString(txn, dbReferrers, addPrefix(*j, *i), "");
                }
                if (validPaths.find(*j) == validPaths.end()) {
                    printMsg(lvlError, format("incomplete closure: `%1%' needs missing `%2%'")
                        % *i % *j);
                }
            }
        }
    }

    /* Check the `referrers' table. */
    printMsg(lvlInfo, "checking the referrers table");
    Strings referrers;
    nixDB.enumTable(txn, dbReferrers, referrers);
    for (Strings::iterator i = referrers.begin(); i != referrers.end(); ++i) {

        /* Decode the entry (it's a tuple of paths). */
        string::size_type nul = i->find((char) 0);
        if (nul == string::npos) {
            printMsg(lvlError, format("removing bad referrer table entry `%1%'") % *i);
            nixDB.delPair(txn, dbReferrers, *i);
            continue;
        }
        Path to(*i, 0, nul);
        Path from(*i, nul + 1);
        
        if (validPaths.find(to) == validPaths.end()) {
            printMsg(lvlError, format("removing referrer entry from `%1%' to invalid `%2%'")
                % from % to);
            nixDB.delPair(txn, dbReferrers, *i);
        }

        else if (validPaths.find(from) == validPaths.end()) {
            printMsg(lvlError, format("removing referrer entry from invalid `%1%' to `%2%'")
                % from % to);
            nixDB.delPair(txn, dbReferrers, *i);
        }
        
        else {
            PathSet references;
            queryReferences(txn, from, references);
            if (find(references.begin(), references.end(), to) == references.end()) {
                printMsg(lvlError, format("adding missing referrer mapping from `%1%' to `%2%'")
                    % from % to);
                references.insert(to);
                setReferences(txn, from, references);
            }
        }
        
    }

    
    txn.commit();
}


typedef std::map<Hash, std::pair<Path, ino_t> > HashToPath;


static void toggleWritable(const Path & path, bool writable)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    mode_t mode = st.st_mode;
    if (writable) mode |= S_IWUSR;
    else mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    
    if (chmod(path.c_str(), mode) == -1)
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

            toggleWritable(dirOf(path), true);
        
            if (link(prevPath.first.c_str(), tempLink.c_str()) == -1)
                throw SysError(format("cannot link `%1%' to `%2%'")
                    % tempLink % prevPath.first);

            /* Atomically replace the old file with the new hard link. */
            if (rename(tempLink.c_str(), path.c_str()) == -1)
                throw SysError(format("cannot rename `%1%' to `%2%'")
                    % tempLink % path);

            /* Make the directory read-only again and reset its
               timestamp back to 0. */
            _canonicalisePathMetaData(dirOf(path), false);
            
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
    
    Paths paths;
    PathSet validPaths;
    nixDB.enumTable(noTxn, dbValidPaths, paths);

    for (Paths::iterator i = paths.begin(); i != paths.end(); ++i) {
        addTempRoot(*i);
        if (!isValidPath(*i)) continue; /* path was GC'ed, probably */
        startNest(nest, lvlChatty, format("hashing files in `%1%'") % *i);
        hashAndLink(dryRun, hashToPath, stats, *i);
    }
}


/* Upgrade from schema 1 (Nix <= 0.7) to schema 2 (Nix >= 0.8). */
static void upgradeStore07()
{
    printMsg(lvlError, "upgrading Nix store to new schema (this may take a while)...");

    Transaction txn(nixDB);

    Paths validPaths2;
    nixDB.enumTable(txn, dbValidPaths, validPaths2);
    PathSet validPaths(validPaths2.begin(), validPaths2.end());

    std::cerr << "hashing paths...";
    int n = 0;
    for (PathSet::iterator i = validPaths.begin(); i != validPaths.end(); ++i) {
        checkInterrupt();
        string s;
        nixDB.queryString(txn, dbValidPaths, *i, s);
        if (s == "") {
            Hash hash = hashPath(htSHA256, *i);
            setHash(txn, *i, hash);
            std::cerr << ".";
            if (++n % 1000 == 0) {
                txn.commit();
                txn.begin(nixDB);
            }
        }
    }
    std::cerr << std::endl;

    txn.commit();

    txn.begin(nixDB);
    
    std::cerr << "processing closures...";
    for (PathSet::iterator i = validPaths.begin(); i != validPaths.end(); ++i) {
        checkInterrupt();
        if (i->size() > 6 && string(*i, i->size() - 6) == ".store") {
            ATerm t = ATreadFromNamedFile(i->c_str());
            if (!t) throw Error(format("cannot read aterm from `%1%'") % *i);

            ATermList roots, elems;
            if (!matchOldClosure(t, roots, elems)) continue;

            for (ATermIterator j(elems); j; ++j) {

                ATerm path2;
                ATermList references2;
                if (!matchOldClosureElem(*j, path2, references2)) continue;

                Path path = aterm2String(path2);
                if (validPaths.find(path) == validPaths.end())
                    /* Skip this path; it's invalid.  This is a normal
                       condition (Nix <= 0.7 did not enforce closure
                       on closure store expressions). */
                    continue;

                PathSet references;
                for (ATermIterator k(references2); k; ++k) {
                    Path reference = aterm2String(*k);
                    if (validPaths.find(reference) == validPaths.end())
                        /* Bad reference.  Set it anyway and let the
                           user fix it. */
                        printMsg(lvlError, format("closure `%1%' contains reference from `%2%' "
                                     "to invalid path `%3%' (run `nix-store --verify')")
                            % *i % path % reference);
                    references.insert(reference);
                }

                PathSet prevReferences;
                queryReferences(txn, path, prevReferences);
                if (prevReferences.size() > 0 && references != prevReferences)
                    printMsg(lvlError, format("warning: conflicting references for `%1%'") % path);

                if (references != prevReferences)
                    setReferences(txn, path, references);
            }
            
            std::cerr << ".";
        }
    }
    std::cerr << std::endl;

    /* !!! maybe this transaction is way too big */
    txn.commit();
}


/* Upgrade from schema 2 (0.8 <= Nix <= 0.9) to schema 3 (Nix >=
   0.10).  The only thing to do here is to upgrade the old `referer'
   table (which causes quadratic complexity in some cases) to the new
   (and properly spelled) `referrer' table. */
static void upgradeStore09() 
{
    /* !!! we should disallow concurrent upgrades */
    
    if (!pathExists(nixDBPath + "/referers")) return;

    printMsg(lvlError, "upgrading Nix store to new schema (this may take a while)...");

    Transaction txn(nixDB);

    std::cerr << "converting referers to referrers...";

    TableId dbReferers = nixDB.openTable("referers"); /* sic! */

    Paths referersKeys;
    nixDB.enumTable(txn, dbReferers, referersKeys);

    int n = 0;
    for (Paths::iterator i = referersKeys.begin();
         i != referersKeys.end(); ++i)
    {
        Paths referers;
        nixDB.queryStrings(txn, dbReferers, *i, referers);
        for (Paths::iterator j = referers.begin();
             j != referers.end(); ++j)
            nixDB.setString(txn, dbReferrers, addPrefix(*i, *j), "");
        if (++n % 1000 == 0) {
            txn.commit();
            txn.begin(nixDB);
            std::cerr << "|";
        }
        std::cerr << ".";
    }

    txn.commit();
    
    std::cerr << std::endl;

    nixDB.closeTable(dbReferers);

    nixDB.deleteTable("referers");
}

 
/* Upgrade from schema 3 (Nix 0.10) to schema 4 (Nix >= 0.11).  The
   only thing to do here is to delete the substitutes table and get
   rid of invalid but substitutable references/referrers.  */
static void upgradeStore11()
{
    if (!pathExists(nixDBPath + "/substitutes")) return;

    printMsg(lvlError, "upgrading Nix store to new schema (this may take a while)...");

    Transaction txn(nixDB);
    TableId dbSubstitutes = nixDB.openTable("substitutes");

    Paths subKeys;
    nixDB.enumTable(txn, dbSubstitutes, subKeys);
    for (Paths::iterator i = subKeys.begin(); i != subKeys.end(); ++i) {
        if (!isValidPathTxn(txn, *i))
            invalidatePath(txn, *i);
    }
    
    txn.commit();
    nixDB.closeTable(dbSubstitutes);
    nixDB.deleteTable("substitutes");
}


}
