#include <iostream>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "store.hh"
#include "globals.hh"
#include "db.hh"
#include "archive.hh"
#include "pathlocks.hh"
#include "gc.hh"


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

/* dbReferers :: Path -> [Path]

   This table is just the reverse mapping of dbReferences. */
static TableId dbReferers = 0;

/* dbSubstitutes :: Path -> [[Path]]

   Each pair $(p, subs)$ tells Nix that it can use any of the
   substitutes in $subs$ to build path $p$.  Each substitute defines a
   command-line invocation of a program (i.e., the first list element
   is the full path to the program, the remaining elements are
   arguments).

   The main purpose of this is for distributed caching of derivates.
   One system can compute a derivate and put it on a website (as a Nix
   archive), for instance, and then another system can register a
   substitute for that derivate.  The substitute in this case might be
   a Nix derivation that fetches the Nix archive.
*/
static TableId dbSubstitutes = 0;

/* dbDerivers :: Path -> [Path]

   This table lists the derivation used to build a path.  There can
   only be multiple such paths for fixed-output derivations (i.e.,
   derivations specifying an expected hash). */
static TableId dbDerivers = 0;


bool Substitute::operator == (const Substitute & sub)
{
    return program == sub.program
        && args == sub.args;
}


static void upgradeStore();


void openDB()
{
    if (readOnlyMode) return;

    try {
        nixDB.open(nixDBPath);
    } catch (DbNoPermission & e) {
        printMsg(lvlTalkative, "cannot access Nix database; continuing anyway");
        readOnlyMode = true;
        return;
    }
    dbValidPaths = nixDB.openTable("validpaths");
    dbReferences = nixDB.openTable("references");
    dbReferers = nixDB.openTable("referers");
    dbSubstitutes = nixDB.openTable("substitutes");
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
        upgradeStore();
        writeFile(schemaFN, (format("%1%") % nixSchemaVersion).str());
    }
}


void initDB()
{
}


void createStoreTransaction(Transaction & txn)
{
    Transaction txn2(nixDB);
    txn2.moveTo(txn);
}


/* Path copying. */

struct CopySink : DumpSink
{
    string s;
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        s.append((const char *) data, len);
    }
};


struct CopySource : RestoreSource
{
    string & s;
    unsigned int pos;
    CopySource(string & _s) : s(_s), pos(0) { }
    virtual void operator () (unsigned char * data, unsigned int len)
    {
        s.copy((char *) data, len, pos);
        pos += len;
        assert(pos <= s.size());
    }
};


void copyPath(const Path & src, const Path & dst)
{
    debug(format("copying `%1%' to `%2%'") % src % dst);

    /* Dump an archive of the path `src' into a string buffer, then
       restore the archive to `dst'.  This is not a very good method
       for very large paths, but `copyPath' is mainly used for small
       files. */ 

    CopySink sink;
    {
        SwitchToOriginalUser sw;
        dumpPath(src, sink);
    }

    CopySource source(sink.s);
    restorePath(dst, source);
}


bool isInStore(const Path & path)
{
    return path[0] == '/'
        && path.compare(0, nixStore.size(), nixStore) == 0
        && path.size() >= nixStore.size() + 2
        && path[nixStore.size()] == '/';
}


bool isStorePath(const Path & path)
{
    return isInStore(path)
        && path.find('/', nixStore.size() + 1) == Path::npos;
}


void assertStorePath(const Path & path)
{
    if (!isStorePath(path))
        throw Error(format("path `%1%' is not in the Nix store") % path);
}


Path toStorePath(const Path & path)
{
    if (!isInStore(path))
        throw Error(format("path `%1%' is not in the Nix store") % path);
    unsigned int slash = path.find('/', nixStore.size() + 1);
    if (slash == Path::npos)
        return path;
    else
        return Path(path, 0, slash);
}


void checkStoreName(const string & name)
{
    string validChars = "+-._?=";
    for (string::const_iterator i = name.begin(); i != name.end(); ++i)
        if (!((*i >= 'A' && *i <= 'Z') ||
              (*i >= 'a' && *i <= 'z') ||
              (*i >= '0' && *i <= '9') ||
              validChars.find(*i) != string::npos))
        {
            throw Error(format("invalid character `%1%' in name `%2%'")
                % *i % name);
        }
}


void canonicalisePathMetaData(const Path & path)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

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

        if (st.st_uid != getuid() || st.st_gid != getgid()) {
            if (chown(path.c_str(), getuid(), getgid()) == -1)
                throw SysError(format("changing owner/group of `%1%' to %2%/%3%")
                    % path % getuid() % getgid());
        }

        if (st.st_mtime != 0) {
            struct utimbuf utimbuf;
            utimbuf.actime = st.st_atime;
            utimbuf.modtime = 0;
            if (utime(path.c_str(), &utimbuf) == -1) 
                throw SysError(format("changing modification time of `%1%'") % path);
        }

    }

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
	    canonicalisePathMetaData(path + "/" + *i);
    }
}


bool isValidPathTxn(const Transaction & txn, const Path & path)
{
    string s;
    return nixDB.queryString(txn, dbValidPaths, path, s);
}


bool isValidPath(const Path & path)
{
    return isValidPathTxn(noTxn, path);
}


static Substitutes readSubstitutes(const Transaction & txn,
    const Path & srcPath);


static bool isRealisablePath(const Transaction & txn, const Path & path)
{
    return isValidPathTxn(txn, path)
        || readSubstitutes(txn, path).size() > 0;
}


static PathSet getReferers(const Transaction & txn, const Path & storePath)
{
    Paths referers;
    nixDB.queryStrings(txn, dbReferers, storePath, referers);
    return PathSet(referers.begin(), referers.end());
}


void setReferences(const Transaction & txn, const Path & storePath,
    const PathSet & references)
{
    /* For unrealisable paths, we can only clear the references. */
    if (references.size() > 0 && !isRealisablePath(txn, storePath))
        throw Error(
            format("cannot set references for path `%1%' which is invalid and has no substitutes")
            % storePath);

    Paths oldReferences;
    nixDB.queryStrings(txn, dbReferences, storePath, oldReferences);

    PathSet oldReferences2(oldReferences.begin(), oldReferences.end());
    if (oldReferences2 == references) return;
    
    nixDB.setStrings(txn, dbReferences, storePath,
        Paths(references.begin(), references.end()));

    /* Update the referers mappings of all referenced paths. */
    for (PathSet::const_iterator i = references.begin();
         i != references.end(); ++i)
    {
        PathSet referers = getReferers(txn, *i);
        referers.insert(storePath);
        nixDB.setStrings(txn, dbReferers, *i,
            Paths(referers.begin(), referers.end()));
    }

    /* Remove referer mappings from paths that are no longer
       references. */
    for (Paths::iterator i = oldReferences.begin();
         i != oldReferences.end(); ++i)
        if (references.find(*i) == references.end()) {
            PathSet referers = getReferers(txn, *i);
            referers.erase(storePath);
            nixDB.setStrings(txn, dbReferers, *i,
                Paths(referers.begin(), referers.end()));
        }
}


void queryReferences(const Transaction & txn,
    const Path & storePath, PathSet & references)
{
    Paths references2;
    if (!isRealisablePath(txn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    nixDB.queryStrings(txn, dbReferences, storePath, references2);
    references.insert(references2.begin(), references2.end());
}


void queryReferers(const Transaction & txn,
    const Path & storePath, PathSet & referers)
{
    if (!isRealisablePath(txn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    PathSet referers2 = getReferers(txn, storePath);
    referers.insert(referers2.begin(), referers2.end());
}


void setDeriver(const Transaction & txn, const Path & storePath,
    const Path & deriver)
{
    assertStorePath(storePath);
    if (deriver == "") return;
    assertStorePath(deriver);
    if (!isRealisablePath(txn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    nixDB.setString(txn, dbDerivers, storePath, deriver);
}


Path queryDeriver(const Transaction & txn, const Path & storePath)
{
    if (!isRealisablePath(txn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    Path deriver;
    if (nixDB.queryString(txn, dbDerivers, storePath, deriver))
        return deriver;
    else
        return "";
}


const int substituteVersion = 2;


static Substitutes readSubstitutes(const Transaction & txn,
    const Path & srcPath)
{
    Strings ss;
    nixDB.queryStrings(txn, dbSubstitutes, srcPath, ss);

    Substitutes subs;
    
    for (Strings::iterator i = ss.begin(); i != ss.end(); ++i) {
        if (i->size() < 4 || (*i)[3] != 0) {
            /* Old-style substitute.  !!! remove this code
               eventually? */
            break;
        }
        Strings ss2 = unpackStrings(*i);
        if (ss2.size() == 0) continue;
        int version;
        if (!string2Int(ss2.front(), version)) continue;
        if (version != substituteVersion) continue;
        if (ss2.size() != 4) throw Error("malformed substitute");
        Strings::iterator j = ss2.begin();
        j++;
        Substitute sub;
        sub.deriver = *j++;
        sub.program = *j++;
        sub.args = unpackStrings(*j++);
        subs.push_back(sub);
    }

    return subs;
}


static void writeSubstitutes(const Transaction & txn,
    const Path & srcPath, const Substitutes & subs)
{
    Strings ss;

    for (Substitutes::const_iterator i = subs.begin();
         i != subs.end(); ++i)
    {
        Strings ss2;
        ss2.push_back((format("%1%") % substituteVersion).str());
        ss2.push_back(i->deriver);
        ss2.push_back(i->program);
        ss2.push_back(packStrings(i->args));
        ss.push_back(packStrings(ss2));
    }

    nixDB.setStrings(txn, dbSubstitutes, srcPath, ss);
}


void registerSubstitute(const Transaction & txn,
    const Path & srcPath, const Substitute & sub)
{
    assertStorePath(srcPath);
    
    Substitutes subs = readSubstitutes(txn, srcPath);

    if (find(subs.begin(), subs.end(), sub) != subs.end())
        return;

    /* New substitutes take precedence over old ones.  If the
       substitute is already present, it's moved to the front. */
    remove(subs.begin(), subs.end(), sub);
    subs.push_front(sub);
        
    writeSubstitutes(txn, srcPath, subs);
}


Substitutes querySubstitutes(const Transaction & txn, const Path & srcPath)
{
    return readSubstitutes(txn, srcPath);
}


static void invalidatePath(Transaction & txn, const Path & path);


void clearSubstitutes()
{
    Transaction txn(nixDB);
    
    /* Iterate over all paths for which there are substitutes. */
    Paths subKeys;
    nixDB.enumTable(txn, dbSubstitutes, subKeys);
    for (Paths::iterator i = subKeys.begin(); i != subKeys.end(); ++i) {
        
        /* Delete all substitutes for path *i. */
        nixDB.delPair(txn, dbSubstitutes, *i);
        
        /* Maintain the cleanup invariant. */
        if (!isValidPathTxn(txn, *i))
            invalidatePath(txn, *i);
    }

    /* !!! there should be no referers to any of the invalid
       substitutable paths.  This should be the case by construction
       (the only referers can be other invalid substitutable paths,
       which have all been removed now). */
    
    txn.commit();
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
    unsigned int colon = s.find(':');
    if (colon == string::npos)
        throw Error(format("corrupt hash `%1%' in valid-path entry for `%2%'")
            % s % storePath);
    HashType ht = parseHashType(string(s, 0, colon));
    if (ht == htUnknown)
        throw Error(format("unknown hash type `%1%' in valid-path entry for `%2%'")
            % string(s, 0, colon) % storePath);
    return parseHash(ht, string(s, colon + 1));
}


Hash queryPathHash(const Path & path)
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
   there are no referers. */
static void invalidatePath(Transaction & txn, const Path & path)
{
    debug(format("unregistering path `%1%'") % path);

    /* Clear the `references' entry for this path, as well as the
       inverse `referers' entries, and the `derivers' entry; but only
       if there are no substitutes for this path.  This maintains the
       cleanup invariant. */
    if (querySubstitutes(txn, path).size() == 0) {
        setReferences(txn, path, PathSet());
        nixDB.delPair(txn, dbDerivers, path);
    }
    
    nixDB.delPair(txn, dbValidPaths, path);
}


Path makeStorePath(const string & type,
    const Hash & hash, const string & suffix)
{
    /* e.g., "source:sha256:1abc...:/nix/store:foo.tar.gz" */
    string s = type + ":sha256:" + printHash(hash) + ":"
        + nixStore + ":" + suffix;

    checkStoreName(suffix);

    return nixStore + "/"
        + printHash32(compressHash(hashString(htSHA256, s), 20))
        + "-" + suffix;
}


Path makeFixedOutputPath(bool recursive,
    string hashAlgo, Hash hash, string name)
{
    /* !!! copy/paste from primops.cc */
    Hash h = hashString(htSHA256, "fixed:out:"
        + (recursive ? (string) "r:" : "") + hashAlgo + ":"
        + printHash(hash) + ":"
        + "");
    return makeStorePath("output:out", h, name);
}


static Path _addToStore(bool fixed, bool recursive,
    string hashAlgo, const Path & _srcPath)
{
    Path srcPath(absPath(_srcPath));
    debug(format("adding `%1%' to the store") % srcPath);

    Hash h(htSHA256);
    {
        SwitchToOriginalUser sw;
        h = hashPath(htSHA256, srcPath);
    }

    string baseName = baseNameOf(srcPath);

    Path dstPath;
    
    if (fixed) {

        HashType ht(parseHashType(hashAlgo));
        Hash h2(ht);
        {
            SwitchToOriginalUser sw;
            h2 = recursive ? hashPath(ht, srcPath) : hashFile(ht, srcPath);
        }
        
        dstPath = makeFixedOutputPath(recursive, hashAlgo, h2, baseName);
    }
        
    else dstPath = makeStorePath("source", h, baseName);

    addTempRoot(dstPath);

    if (!readOnlyMode && !isValidPath(dstPath)) { 

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        PathSet lockPaths;
        lockPaths.insert(dstPath);
        PathLocks outputLock(lockPaths);

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePath(dstPath);

            copyPath(srcPath, dstPath);

            Hash h2 = hashPath(htSHA256, dstPath);
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


Path addToStore(const Path & srcPath)
{
    return _addToStore(false, false, "", srcPath);
}


Path addToStoreFixed(bool recursive, string hashAlgo, const Path & srcPath)
{
    return _addToStore(true, recursive, hashAlgo, srcPath);
}


Path addTextToStore(const string & suffix, const string & s,
    const PathSet & references)
{
    Hash hash = hashString(htSHA256, s);

    Path dstPath = makeStorePath("text", hash, suffix);
    
    addTempRoot(dstPath);

    if (!readOnlyMode && !isValidPath(dstPath)) {

        PathSet lockPaths;
        lockPaths.insert(dstPath);
        PathLocks outputLock(lockPaths);

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePath(dstPath);

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


void deleteFromStore(const Path & _path)
{
    Path path(canonPath(_path));

    assertStorePath(path);

    Transaction txn(nixDB);
    if (isValidPathTxn(txn, path)) {
        PathSet referers = getReferers(txn, path);
        for (PathSet::iterator i = referers.begin();
             i != referers.end(); ++i)
            if (*i != path && isValidPathTxn(txn, *i))
                throw Error(format("cannot delete path `%1%' because it is in use by path `%2%'") % path % *i);
        invalidatePath(txn, path);
    }
    txn.commit();

    deletePath(path);
}


void verifyStore(bool checkContents)
{
    Transaction txn(nixDB);

    Paths paths;
    PathSet validPaths;
    nixDB.enumTable(txn, dbValidPaths, paths);

    for (Paths::iterator i = paths.begin(); i != paths.end(); ++i) {
        if (!pathExists(*i)) {
            printMsg(lvlError, format("path `%1%' disappeared") % *i);
            invalidatePath(txn, *i);
        } else if (!isStorePath(*i)) {
            printMsg(lvlError, format("path `%1%' is not in the Nix store") % *i);
            invalidatePath(txn, *i);
        } else {
            if (checkContents) {
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

    /* "Usable" paths are those that are valid or have a
       substitute. */
    PathSet usablePaths(validPaths);

    /* Check that the values of the substitute mappings are valid
       paths. */ 
    Paths subKeys;
    nixDB.enumTable(txn, dbSubstitutes, subKeys);
    for (Paths::iterator i = subKeys.begin(); i != subKeys.end(); ++i) {
        Substitutes subs = readSubstitutes(txn, *i);
        if (!isStorePath(*i)) {
            printMsg(lvlError, format("found substitutes for non-store path `%1%'") % *i);
            nixDB.delPair(txn, dbSubstitutes, *i);
        }
        else if (subs.size() == 0)
            nixDB.delPair(txn, dbSubstitutes, *i);
        else
	    usablePaths.insert(*i);
    }

    /* Check the cleanup invariant: only usable paths can have
       `references', `referers', or `derivers' entries. */

    /* Check the `derivers' table. */
    Paths deriversKeys;
    nixDB.enumTable(txn, dbDerivers, deriversKeys);
    for (Paths::iterator i = deriversKeys.begin();
         i != deriversKeys.end(); ++i)
    {
        if (usablePaths.find(*i) == usablePaths.end()) {
            printMsg(lvlError, format("found deriver entry for unusable path `%1%'")
                % *i);
            nixDB.delPair(txn, dbDerivers, *i);
        }
        else {
            Path deriver = queryDeriver(txn, *i);
            if (!isStorePath(deriver)) {
                printMsg(lvlError, format("found corrupt deriver `%1%' for `%2%'")
                    % deriver % *i);
                nixDB.delPair(txn, dbDerivers, *i);
            }
        }
    }

    /* Check the `references' table. */
    Paths referencesKeys;
    nixDB.enumTable(txn, dbReferences, referencesKeys);
    for (Paths::iterator i = referencesKeys.begin();
         i != referencesKeys.end(); ++i)
    {
        if (usablePaths.find(*i) == usablePaths.end()) {
            printMsg(lvlError, format("found references entry for unusable path `%1%'")
                % *i);
            setReferences(txn, *i, PathSet());
        }
        else {
            bool isValid = validPaths.find(*i) != validPaths.end();
            PathSet references;
            queryReferences(txn, *i, references);
            for (PathSet::iterator j = references.begin();
                 j != references.end(); ++j)
            {
                PathSet referers = getReferers(txn, *j);
                if (referers.find(*i) == referers.end()) {
                    printMsg(lvlError, format("missing referer mapping from `%1%' to `%2%'")
                        % *j % *i);
                }
                if (isValid && validPaths.find(*j) == validPaths.end()) {
                    printMsg(lvlError, format("incomplete closure: `%1%' needs missing `%2%'")
                        % *i % *j);
                }
            }
        }
    }

    /* Check the `referers' table. */
    Paths referersKeys;
    nixDB.enumTable(txn, dbReferers, referersKeys);
    for (Paths::iterator i = referersKeys.begin();
         i != referersKeys.end(); ++i)
    {
        if (usablePaths.find(*i) == usablePaths.end()) {
            printMsg(lvlError, format("found referers entry for unusable path `%1%'")
                % *i);
            nixDB.delPair(txn, dbReferers, *i);
        }
        else {
            PathSet referers, newReferers;
            queryReferers(txn, *i, referers);
            for (PathSet::iterator j = referers.begin();
                 j != referers.end(); ++j)
            {
                Paths references;
                if (usablePaths.find(*j) == usablePaths.end()) {
                    printMsg(lvlError, format("referer mapping from `%1%' to unusable `%2%'")
                        % *i % *j);
                } else {
                    nixDB.queryStrings(txn, dbReferences, *j, references);
                    if (find(references.begin(), references.end(), *i) == references.end()) {
                        printMsg(lvlError, format("missing reference mapping from `%1%' to `%2%'")
                            % *j % *i);
                        /* !!! repair by inserting *i into references */
                    }
                    else newReferers.insert(*j);
                }
            }
            if (referers != newReferers)
                nixDB.setStrings(txn, dbReferers, *i,
                    Paths(newReferers.begin(), newReferers.end()));
        }
    }

    txn.commit();
}


#include "aterm.hh"
#include "derivations-ast.hh"


/* Upgrade from schema 1 (Nix <= 0.7) to schema 2 (Nix >= 0.8). */
static void upgradeStore()
{
    printMsg(lvlError, "upgrading Nix store to new schema (this may take a while)...");

    Transaction txn(nixDB);

    Paths validPaths2;
    nixDB.enumTable(txn, dbValidPaths, validPaths2);
    PathSet validPaths(validPaths2.begin(), validPaths2.end());

    cerr << "hashing paths...";
    int n = 0;
    for (PathSet::iterator i = validPaths.begin(); i != validPaths.end(); ++i) {
        checkInterrupt();
        string s;
        nixDB.queryString(txn, dbValidPaths, *i, s);
        if (s == "") {
            Hash hash = hashPath(htSHA256, *i);
            setHash(txn, *i, hash);
            cerr << ".";
            if (++n % 1000 == 0) {
                txn.commit();
                txn.begin(nixDB);
            }
        }
    }
    cerr << "\n";

    txn.commit();

    txn.begin(nixDB);
    
    cerr << "processing closures...";
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
            
            cerr << ".";
        }
    }
    cerr << "\n";

    /* !!! maybe this transaction is way too big */
    txn.commit();
}
