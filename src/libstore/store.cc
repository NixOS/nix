#include <iostream>
#include <algorithm>

#include <sys/wait.h>
#include <unistd.h>

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

/* dbSuccessors :: Path -> Path

   Each pair $(p_1, p_2)$ in this mapping records the fact that the
   Nix expression stored at path $p_1$ has a successor expression
   stored at path $p_2$.

   Note that a term $y$ is a successor of $x$ iff there exists a
   sequence of rewrite steps that rewrites $x$ into $y$.
*/
static TableId dbSuccessors = 0;

/* dbSuccessorsRev :: Path -> [Path]

   The reverse mapping of dbSuccessors (i.e., it stores the
   predecessors of a Nix expression).
*/
static TableId dbSuccessorsRev = 0;

/* dbSubstitutes :: Path -> [(Path, Path, [string])]

   Each pair $(p, subs)$ tells Nix that it can use any of the
   substitutes in $subs$ to build path $p$.  Each substitute is a
   tuple $(storeExpr, program, args)$ (see the type `Substitute' in
   `store.hh').

   The main purpose of this is for distributed caching of derivates.
   One system can compute a derivate and put it on a website (as a Nix
   archive), for instance, and then another system can register a
   substitute for that derivate.  The substitute in this case might be
   a Nix expression that fetches the Nix archive.
*/
static TableId dbSubstitutes = 0;

/* dbSubstitutesRev :: Path -> [Path]

   The reverse mapping of dbSubstitutes; it maps store expressions
   back to the paths for which they are substitutes.
*/
static TableId dbSubstitutesRev = 0;


bool Substitute::operator == (const Substitute & sub)
{
    return storeExpr == sub.storeExpr
        && program == sub.program
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
    dbSuccessors = nixDB.openTable("successors");
    dbSuccessorsRev = nixDB.openTable("successors-rev");
    dbSubstitutes = nixDB.openTable("substitutes");
    dbSubstitutesRev = nixDB.openTable("substitutes-rev");
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


static bool isValidPathTxn(const Path & path, const Transaction & txn)
{
    string s;
    return nixDB.queryString(txn, dbValidPaths, path, s);
}


bool isValidPath(const Path & path)
{
    return isValidPathTxn(path, noTxn);
}


static bool isUsablePathTxn(const Path & path, const Transaction & txn)
{
    if (isValidPathTxn(path, txn)) return true;
    Paths subs;
    nixDB.queryStrings(txn, dbSubstitutes, path, subs);
    return subs.size() > 0;
}


void registerSuccessor(const Transaction & txn,
    const Path & srcPath, const Path & sucPath)
{
    assertStorePath(srcPath);
    assertStorePath(sucPath);
    
    if (!isUsablePathTxn(sucPath, txn))	throw Error(
	format("path `%1%' cannot be a successor, since it is not usable")
	% sucPath);

    Path known;
    if (nixDB.queryString(txn, dbSuccessors, srcPath, known) &&
        known != sucPath)
    {
        throw Error(format(
            "the `impossible' happened: expression in path "
            "`%1%' appears to have multiple successors "
            "(known `%2%', new `%3%'")
            % srcPath % known % sucPath);
    }

    Paths revs;
    nixDB.queryStrings(txn, dbSuccessorsRev, sucPath, revs);
    if (find(revs.begin(), revs.end(), srcPath) == revs.end())
        revs.push_back(srcPath);

    nixDB.setString(txn, dbSuccessors, srcPath, sucPath);
    nixDB.setStrings(txn, dbSuccessorsRev, sucPath, revs);
}


void unregisterSuccessor(const Path & srcPath)
{
    assertStorePath(srcPath);

    Transaction txn(nixDB);

    Path sucPath;
    if (!nixDB.queryString(txn, dbSuccessors, srcPath, sucPath)) {
        txn.abort();
        return;
    }
    nixDB.delPair(txn, dbSuccessors, srcPath);

    Paths revs;
    nixDB.queryStrings(txn, dbSuccessorsRev, sucPath, revs);
    Paths::iterator i = find(revs.begin(), revs.end(), srcPath);
    assert(i != revs.end());
    revs.erase(i);
    nixDB.setStrings(txn, dbSuccessorsRev, sucPath, revs);

    txn.commit();
}


bool querySuccessor(const Path & srcPath, Path & sucPath)
{
    return nixDB.queryString(noTxn, dbSuccessors, srcPath, sucPath);
}


Paths queryPredecessors(const Path & sucPath)
{
    Paths revs;
    nixDB.queryStrings(noTxn, dbSuccessorsRev, sucPath, revs);
    return revs;
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
        if (ss2.size() != 3) throw Error("malformed substitute");
        Strings::iterator j = ss2.begin();
        Substitute sub;
        sub.storeExpr = *j++;
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
        ss2.push_back(i->storeExpr);
        ss2.push_back(i->program);
        ss2.push_back(packStrings(i->args));
        ss.push_back(packStrings(ss2));
    }

    nixDB.setStrings(txn, dbSubstitutes, srcPath, ss);
}


typedef map<Path, Paths> SubstitutesRev;


void registerSubstitutes(const Transaction & txn,
    const SubstitutePairs & subPairs)
{
    SubstitutesRev revMap;
    
    for (SubstitutePairs::const_iterator i = subPairs.begin();
         i != subPairs.end(); ++i)
    {
        const Path & srcPath(i->first);
        const Substitute & sub(i->second);

        assertStorePath(srcPath);
        assertStorePath(sub.storeExpr);
    
        Substitutes subs = readSubstitutes(txn, srcPath);

        /* New substitutes take precedence over old ones.  If the
           substitute is already present, it's moved to the front. */
        remove(subs.begin(), subs.end(), sub);
        subs.push_front(sub);
        
        writeSubstitutes(txn, srcPath, subs);

        Paths & revs = revMap[sub.storeExpr];
        if (revs.empty())
            nixDB.queryStrings(txn, dbSubstitutesRev, sub.storeExpr, revs);
        if (find(revs.begin(), revs.end(), srcPath) == revs.end())
            revs.push_back(srcPath);
    }

    /* Re-write the reverse mapping in one go to prevent Theta(n^2)
       performance.  (This would occur because the data fields of the
       `substitutes-rev' table are lists). */
    for (SubstitutesRev::iterator i = revMap.begin(); i != revMap.end(); ++i)
        nixDB.setStrings(txn, dbSubstitutesRev, i->first, i->second);
}


Substitutes querySubstitutes(const Path & srcPath)
{
    return readSubstitutes(noTxn, srcPath);
}


void registerValidPath(const Transaction & txn, const Path & _path)
{
    Path path(canonPath(_path));
    assertStorePath(path);
    debug(format("registering path `%1%'") % path);
    nixDB.setString(txn, dbValidPaths, path, "");
}


static void invalidatePath(const Path & path, Transaction & txn)
{
    debug(format("unregistering path `%1%'") % path);

    nixDB.delPair(txn, dbValidPaths, path);

    /* Remove any successor mappings to this path (but not *from*
       it). */
    Paths revs;
    nixDB.queryStrings(txn, dbSuccessorsRev, path, revs);
    for (Paths::iterator i = revs.begin(); i != revs.end(); ++i)
        nixDB.delPair(txn, dbSuccessors, *i);
    nixDB.delPair(txn, dbSuccessorsRev, path);

    /* Remove any substitute mappings to this path. */
    revs.clear();
    nixDB.queryStrings(txn, dbSubstitutesRev, path, revs);
    for (Paths::iterator i = revs.begin(); i != revs.end(); ++i) {
        Substitutes subs = readSubstitutes(txn, *i), subs2;
        bool found = false;
        for (Substitutes::iterator j = subs.begin(); j != subs.end(); ++j)
            if (j->storeExpr != path)
                subs2.push_back(*j);
            else
                found = true;
        // !!!        if (!found) throw Error("integrity error in substitutes mapping");
        writeSubstitutes(txn, *i, subs);

	/* If path *i now has no substitutes left, and is not valid,
	   then it too should be invalidated.  This is because it may
	   be a substitute or successor. */
	if (subs.size() == 0 && !isValidPathTxn(*i, txn))
	    invalidatePath(*i, txn);
    }
    nixDB.delPair(txn, dbSubstitutesRev, path);
}


Path addToStore(const Path & _srcPath)
{
    Path srcPath(absPath(_srcPath));
    debug(format("adding `%1%' to the store") % srcPath);

    Hash h;
    {
        SwitchToOriginalUser sw;
        h = hashPath(srcPath);
    }

    string baseName = baseNameOf(srcPath);
    Path dstPath = canonPath(nixStore + "/" + (string) h + "-" + baseName);

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

            makePathReadOnly(dstPath);
            
            Transaction txn(nixDB);
            registerValidPath(txn, dstPath);
            txn.commit();
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


void addTextToStore(const Path & dstPath, const string & s)
{
    assertStorePath(dstPath);
    
    if (!isValidPath(dstPath)) {

        PathSet lockPaths;
        lockPaths.insert(dstPath);
        PathLocks outputLock(lockPaths);

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePath(dstPath);

            writeStringToFile(dstPath, s);

            makePathReadOnly(dstPath);
            
            Transaction txn(nixDB);
            registerValidPath(txn, dstPath);
            txn.commit();
        }

        outputLock.setDeletion(true);
    }
}


void deleteFromStore(const Path & _path)
{
    Path path(canonPath(_path));

    assertStorePath(path);

    Transaction txn(nixDB);
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
        Substitutes subs = readSubstitutes(txn, *i), subs2;
        for (Substitutes::iterator j = subs.begin(); j != subs.end(); ++j)
            if (validPaths.find(j->storeExpr) == validPaths.end())
                printMsg(lvlError,
                    format("found substitute mapping to non-existent path `%1%'")
                    % j->storeExpr);
            else
                subs2.push_back(*j);
        if (subs.size() != subs2.size())
            writeSubstitutes(txn, *i, subs2);
	if (subs2.size() > 0)
	    usablePaths.insert(*i);
    }

    /* Check that the keys of the reverse substitute mappings are
       valid paths. */ 
    Paths rsubKeys;
    nixDB.enumTable(txn, dbSubstitutesRev, rsubKeys);
    for (Paths::iterator i = rsubKeys.begin(); i != rsubKeys.end(); ++i) {
        if (validPaths.find(*i) == validPaths.end()) {
            printMsg(lvlError,
                format("found reverse substitute mapping for non-existent path `%1%'") % *i);
            nixDB.delPair(txn, dbSubstitutesRev, *i);
        }
    }

    /* Check that the values of the successor mappings are usable
       paths. */ 
    Paths sucKeys;
    nixDB.enumTable(txn, dbSuccessors, sucKeys);
    for (Paths::iterator i = sucKeys.begin(); i != sucKeys.end(); ++i) {
        /* Note that *i itself does not have to be valid, just its
           successor. */
        Path sucPath;
        if (nixDB.queryString(txn, dbSuccessors, *i, sucPath) &&
            usablePaths.find(sucPath) == usablePaths.end())
        {
            printMsg(lvlError,
                format("found successor mapping to non-existent path `%1%'") % sucPath);
            nixDB.delPair(txn, dbSuccessors, *i);
        }
    }

    /* Check that the keys of the reverse successor mappings are valid
       paths. */ 
    Paths rsucKeys;
    nixDB.enumTable(txn, dbSuccessorsRev, rsucKeys);
    for (Paths::iterator i = rsucKeys.begin(); i != rsucKeys.end(); ++i) {
        if (usablePaths.find(*i) == usablePaths.end()) {
            printMsg(lvlError,
                format("found reverse successor mapping for non-existent path `%1%'") % *i);
            nixDB.delPair(txn, dbSuccessorsRev, *i);
        }
    }

    txn.commit();
}
