#include <iostream>
#include <algorithm>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "store.hh"
#include "globals.hh"
#include "db.hh"
#include "archive.hh"
#include "pathlocks.hh"


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


bool Substitute::operator == (const Substitute & sub)
{
    return program == sub.program
        && args == sub.args;
}


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


void copyPath(const Path & src, const Path & dst)
{
    debug(format("copying `%1%' to `%2%'") % src % dst);

    /* Unfortunately C++ doesn't support coprocedures, so we have no
       nice way to chain CopySink and CopySource together.  Instead we
       fork off a child to run the sink.  (Fork-less platforms should
       use a thread). */

    /* Create a pipe. */
    Pipe pipe;
    pipe.create();

    /* Fork. */
    Pid pid;
    pid = fork();
    switch (pid) {

    case -1:
        throw SysError("unable to fork");

    case 0: /* child */
        try {
            pipe.writeSide.close();
            CopySource source;
            source.fd = pipe.readSide;
            restorePath(dst, source);
            _exit(0);
        } catch (exception & e) {
            cerr << "error: " << e.what() << endl;
        }
        _exit(1);        
    }

    /* Parent. */

    pipe.readSide.close();
    
    CopySink sink;
    sink.fd = pipe.writeSide;
    {
        SwitchToOriginalUser sw;
        dumpPath(src, sink);
    }

    /* Wait for the child to finish. */
    int status = pid.wait(true);
    if (!statusOk(status))
        throw Error(format("cannot copy `%1% to `%2%': child %3%")
            % src % dst % statusToString(status));
}


static bool isInStore(const Path & path)
{
    return path[0] == '/'
        && path.compare(0, nixStore.size(), nixStore) == 0
        && path.size() >= nixStore.size() + 2
        && path[nixStore.size()] == '/'
        && path.find('/', nixStore.size() + 1) == Path::npos;
}


void assertStorePath(const Path & path)
{
    if (!isInStore(path))
        throw Error(format("path `%1%' is not in the Nix store") % path);
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


static bool isValidPathTxn(const Transaction & txn, const Path & path)
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
    if (!isRealisablePath(txn, storePath))
        throw Error(
            format("cannot set references for path `%1%' which is invalid and has no substitutes")
            % storePath);

    Paths oldReferences;
    nixDB.queryStrings(txn, dbReferences, storePath, oldReferences);
    
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


void queryReferences(const Path & storePath, PathSet & references)
{
    Paths references2;
    if (!isRealisablePath(noTxn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    nixDB.queryStrings(noTxn, dbReferences, storePath, references2);
    references.insert(references2.begin(), references2.end());
}


void queryReferers(const Path & storePath, PathSet & referers)
{
    Paths referers2;
    if (!isRealisablePath(noTxn, storePath))
        throw Error(format("path `%1%' is not valid") % storePath);
    nixDB.queryStrings(noTxn, dbReferers, storePath, referers2);
    referers.insert(referers2.begin(), referers2.end());
}


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
        if (ss2.size() == 3) {
            /* Another old-style substitute. */
            continue;
        }
        if (ss2.size() != 2) throw Error("malformed substitute");
        Strings::iterator j = ss2.begin();
        Substitute sub;
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


void clearSubstitutes()
{
    Transaction txn(nixDB);
    
    /* Iterate over all paths for which there are substitutes. */
    Paths subKeys;
    nixDB.enumTable(txn, dbSubstitutes, subKeys);
    for (Paths::iterator i = subKeys.begin(); i != subKeys.end(); ++i) {
        /* Delete all substitutes for path *i. */
        nixDB.delPair(txn, dbSubstitutes, *i);
    }

    txn.commit();
}


void registerValidPath(const Transaction & txn,
    const Path & _path, const Hash & hash, const PathSet & references)
{
    Path path(canonPath(_path));
    assertStorePath(path);

    assert(hash.type == htSHA256);
    
    debug(format("registering path `%1%'") % path);
    nixDB.setString(txn, dbValidPaths, path, "sha256:" + printHash(hash));

    setReferences(txn, path, references);
    
    /* Check that all referenced paths are also valid. */
    for (PathSet::iterator i = references.begin(); i != references.end(); ++i)
        if (!isValidPathTxn(txn, *i))
            throw Error(format("cannot register path `%1%' as valid, since its reference `%2%' is invalid")
                % path % *i);
}


static void invalidatePath(const Path & path, Transaction & txn)
{
    debug(format("unregistering path `%1%'") % path);

    /* Clear the `references' entry for this path, as well as the
       inverse `referers' entries; but only if there are no
       substitutes for this path.  This maintains the cleanup
       invariant. */
    if (querySubstitutes(txn, path).size() == 0)
        setReferences(txn, path, PathSet());
    
    nixDB.delPair(txn, dbValidPaths, path);
}


Path makeStorePath(const string & type,
    const Hash & hash, const string & suffix)
{
    /* e.g., "source:sha256:1abc...:/nix/store:foo.tar.gz" */
    string s = type + ":sha256:" + printHash(hash) + ":"
        + nixStore + ":" + suffix;

    return nixStore + "/"
        + printHash32(compressHash(hashString(htSHA256, s), 20))
        + "-" + suffix;
}


Path addToStore(const Path & _srcPath)
{
    Path srcPath(absPath(_srcPath));
    debug(format("adding `%1%' to the store") % srcPath);

    Hash h(htSHA256);
    {
        SwitchToOriginalUser sw;
        h = hashPath(htSHA256, srcPath);
    }

    string baseName = baseNameOf(srcPath);
    Path dstPath = makeStorePath("source", h, baseName);

    if (!readOnlyMode && !isValidPath(dstPath)) { 

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        PathSet lockPaths;
        lockPaths.insert(dstPath);
        PathLocks outputLock(lockPaths);

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePath(dstPath);

            /* !!! race: srcPath might change between hashPath() and
               here! */
            
            copyPath(srcPath, dstPath);

            Hash h2 = hashPath(htSHA256, dstPath);
            if (h != h2)
                throw Error(format("contents of `%1%' changed while copying it to `%2%' (%3% -> %4%)")
                    % srcPath % dstPath % printHash(h) % printHash(h2));

            canonicalisePathMetaData(dstPath);
            
            Transaction txn(nixDB);
            registerValidPath(txn, dstPath, h, PathSet());
            txn.commit();
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


Path addTextToStore(const string & suffix, const string & s,
    const PathSet & references)
{
    Hash hash = hashString(htSHA256, s);

    Path dstPath = makeStorePath("text", hash, suffix);
    
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
                hashPath(htSHA256, dstPath), references);
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
    if (isValidPathTxn(txn, path))
        invalidatePath(path, txn);
    txn.commit();

    deletePath(path);
}


void verifyStore()
{
    Transaction txn(nixDB);

    Paths paths;
    PathSet validPaths;
    nixDB.enumTable(txn, dbValidPaths, paths);

    for (Paths::iterator i = paths.begin(); i != paths.end(); ++i) {
        Path path = *i;
        if (!pathExists(path)) {
            printMsg(lvlError, format("path `%1%' disappeared") % path);
            invalidatePath(path, txn);
        } else if (!isInStore(path)) {
            printMsg(lvlError, format("path `%1%' is not in the Nix store") % path);
            invalidatePath(path, txn);
        } else
            validPaths.insert(path);
    }

    /* !!! the code below does not allow transitive substitutes.
       I.e., if B is a substitute of A, then B must be a valid path.
       B cannot itself be invalid but have a substitute. */

    /* "Usable" paths are those that are valid or have a substitute.
       These are the paths that are allowed to appear in the
       right-hand side of a sute mapping. */
    PathSet usablePaths(validPaths);

    /* Check that the values of the substitute mappings are valid
       paths. */ 
    Paths subKeys;
    nixDB.enumTable(txn, dbSubstitutes, subKeys);
    for (Paths::iterator i = subKeys.begin(); i != subKeys.end(); ++i) {
        Substitutes subs = readSubstitutes(txn, *i);
	if (subs.size() > 0)
	    usablePaths.insert(*i);
        else
            nixDB.delPair(txn, dbSubstitutes, *i);
    }

    txn.commit();
}
