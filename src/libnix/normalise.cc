#include <map>

#include "normalise.hh"
#include "references.hh"
#include "exec.hh"
#include "pathlocks.hh"
#include "globals.hh"


static Path useSuccessor(const Path & path)
{
    string pathSucc;
    if (querySuccessor(path, pathSucc)) {
        debug(format("successor %1% -> %2%") % (string) path % pathSucc);
        return pathSucc;
    } else
        return path;
}


#if 0
/* Return a path whose contents have the given hash.  If target is
   not empty, ensure that such a path is realised in target (if
   necessary by copying from another location).  If prefix is not
   empty, only return a path that is an descendent of prefix. */

string expandId(const FSId & id, const string & target = "",
    const string & prefix = "/", FSIdSet pending = FSIdSet(),
    bool ignoreSubstitutes = false)
{
    xxx
}


string expandId(const FSId & id, const string & target,
    const string & prefix, FSIdSet pending, bool ignoreSubstitutes)
{
    Nest nest(lvlDebug, format("expanding %1%") % (string) id);

    Strings paths;

    if (!target.empty() && !isInPrefix(target, prefix))
        abort();

    nixDB.queryStrings(noTxn, dbId2Paths, id, paths);

    /* Pick one equal to `target'. */
    if (!target.empty()) {

        for (Strings::iterator i = paths.begin();
             i != paths.end(); i++)
        {
            string path = *i;
            if (path == target && pathExists(path))
                return path;
        }
        
    }

    /* Arbitrarily pick the first one that exists and isn't stale. */
    for (Strings::iterator it = paths.begin();
         it != paths.end(); it++)
    {
        string path = *it;
        if (isInPrefix(path, prefix) && pathExists(path)) {
            if (target.empty())
                return path;
            else {
                /* Acquire a lock on the target path. */
                Strings lockPaths;
                lockPaths.push_back(target);
                PathLocks outputLock(lockPaths);

                /* Copy. */
                copyPath(path, target);

                /* Register the target path. */
                Transaction txn(nixDB);
                registerPath(txn, target, id);
                txn.commit();

                return target;
            }
        }
    }

    if (!ignoreSubstitutes) {
        
        if (pending.find(id) != pending.end())
            throw Error(format("id %1% already being expanded") % (string) id);
        pending.insert(id);

        /* Try to realise the substitutes, but only if this id is not
           already being realised by a substitute. */
        Strings subs;
        nixDB.queryStrings(noTxn, dbSubstitutes, id, subs); /* non-existence = ok */

        for (Strings::iterator it = subs.begin(); it != subs.end(); it++) {
            FSId subId = parseHash(*it);

            debug(format("trying substitute %1%") % (string) subId);

            realiseClosure(normaliseNixExpr(subId, pending), pending);

            return expandId(id, target, prefix, pending);
        }

    }
    
    throw Error(format("cannot expand id `%1%'") % (string) id);
}
#endif

    
Path normaliseNixExpr(const Path & _nePath, PathSet pending)
{
    Nest nest(lvlTalkative,
        format("normalising expression in `%1%'") % (string) _nePath);

    /* Try to substitute the expression by any known successors in
       order to speed up the rewrite process. */
    Path nePath = useSuccessor(_nePath);

    /* Get the Nix expression. */
    NixExpr ne = exprFromPath(nePath, pending);

    /* If this is a normal form (i.e., a closure) we are done. */
    if (ne.type == NixExpr::neClosure) return nePath;
    if (ne.type != NixExpr::neDerivation) abort();
    

    /* Otherwise, it's a derivation expression, and we have to build it to
       determine its normal form. */


    /* Some variables. */

    /* Input paths, with their closure elements. */
    ClosureElems inClosures; 

    /* Referenceable paths (i.e., input and output paths). */
    PathSet allPaths;

    /* The environment to be passed to the builder. */
    Environment env; 

    /* The result. */
    NixExpr nf;
    nf.type = NixExpr::neClosure;


    /* The outputs are referenceable paths. */
    for (PathSet::iterator i = ne.derivation.outputs.begin();
         i != ne.derivation.outputs.end(); i++)
    {
        debug(format("building path `%1%'") % *i);
        allPaths.insert(*i);
    }

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes. */
    PathLocks outputLocks(ne.derivation.outputs);

    /* Now check again whether there is a successor.  This is because
       another process may have started building in parallel.  After
       it has finished and released the locks, we can (and should)
       reuse its results.  (Strictly speaking the first successor
       check above can be omitted, but that would be less efficient.)
       Note that since we now hold the locks on the output paths, no
       other process can build this expression, so no further checks
       are necessary. */
    {
        Path nePath2 = useSuccessor(nePath);
        if (nePath != nePath2) {
            NixExpr ne = exprFromPath(nePath2, pending);
            debug(format("skipping build of expression `%1%', someone beat us to it")
		  % (string) nePath);
            if (ne.type != NixExpr::neClosure) abort();
            return nePath2;
        }
    }

    /* Right platform? */
    if (ne.derivation.platform != thisSystem)
        throw Error(format("a `%1%' is required, but I am a `%2%'")
		    % ne.derivation.platform % thisSystem);
        
    /* Realise inputs (and remember all input paths). */
    for (PathSet::iterator i = ne.derivation.inputs.begin();
         i != ne.derivation.inputs.end(); i++)
    {
        Path nfPath = normaliseNixExpr(*i, pending);
        realiseClosure(nfPath, pending);
        /* !!! nfPath should be a root of the garbage collector while
           we are building */
        NixExpr ne = exprFromPath(nfPath, pending);
        if (ne.type != NixExpr::neClosure) abort();
        for (ClosureElems::iterator j = ne.closure.elems.begin();
             j != ne.closure.elems.end(); j++)
	{
            inClosures[j->first] = j->second;
	    allPaths.insert(j->first);
	}
    }

    /* Most shells initialise PATH to some default (/bin:/usr/bin:...) when
       PATH is not set.  We don't want this, so we fill it in with some dummy
       value. */
    env["PATH"] = "/path-not-set";

    /* Set HOME to a non-existing path to prevent certain programs from using
       /etc/passwd (or NIS, or whatever) to locate the home directory (for
       example, wget looks for ~/.wgetrc).  I.e., these tools use /etc/passwd
       if HOME is not set, but they will just assume that the settings file
       they are looking for does not exist if HOME is set but points to some
       non-existing path. */
    env["HOME"] = "/homeless-shelter";

    /* Build the environment. */
    for (StringPairs::iterator i = ne.derivation.env.begin();
         i != ne.derivation.env.end(); i++)
        env[i->first] = i->second;

    /* We can skip running the builder if we can expand all output
       paths from their ids. */
    bool fastBuild = false;
#if 0
    bool fastBuild = true;
    for (PathSet::iterator i = ne.derivation.outputs.begin();
         i != ne.derivation.outputs.end(); i++)
    {
        try {
            expandId(i->second, i->first, "/", pending);
        } catch (Error & e) {
            debug(format("fast build failed for `%1%': %2%")
		  % i->first % e.what());
            fastBuild = false;
            break;
        }
    }
#endif

    if (!fastBuild) {

        /* If any of the outputs already exist but are not registered,
           delete them. */
        for (PathSet::iterator i = ne.derivation.outputs.begin(); 
             i != ne.derivation.outputs.end(); i++)
        {
            Path path = *i;
            if (isValidPath(path))
                throw Error(format("obstructed build: path `%1%' exists") % path);
            if (pathExists(path)) {
                debug(format("removing unregistered path `%1%'") % path);
                deletePath(path);
            }
        }

        /* Run the builder. */
        msg(lvlChatty, format("building..."));
        runProgram(ne.derivation.builder, ne.derivation.args, env);
        msg(lvlChatty, format("build completed"));
        
    } else
        msg(lvlChatty, format("fast build succesful"));

    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    PathSet usedPaths;
    for (PathSet::iterator i = ne.derivation.outputs.begin(); 
         i != ne.derivation.outputs.end(); i++)
    {
        Path path = *i;
        if (!pathExists(path))
            throw Error(format("path `%1%' does not exist") % path);
        nf.closure.roots.insert(path);

	makePathReadOnly(path);

	/* For this output path, find the references to other paths contained
	   in it. */
        Strings refPaths = filterReferences(path, 
            Strings(allPaths.begin(), allPaths.end()));

	/* Construct a closure element for this output path. */
        ClosureElem elem;

	/* For each path referenced by this output path, add its id to the
	   closure element and add the id to the `usedPaths' set (so that the
	   elements referenced by *its* closure are added below). */
        for (Paths::iterator j = refPaths.begin();
	     j != refPaths.end(); j++)
	{
	    Path path = *j;
	    elem.refs.insert(path);
            if (inClosures.find(path) != inClosures.end())
                usedPaths.insert(path);
	    else if (ne.derivation.outputs.find(path) == ne.derivation.outputs.end())
		abort();
        }

        nf.closure.elems[path] = elem;
    }

    /* Close the closure.  That is, for any referenced path, add the paths
       referenced by it. */
    PathSet donePaths;

    while (!usedPaths.empty()) {
	PathSet::iterator i = usedPaths.begin();
	Path path = *i;
	usedPaths.erase(i);

	if (donePaths.find(path) != donePaths.end()) continue;
	donePaths.insert(path);

	ClosureElems::iterator j = inClosures.find(path);
	if (j == inClosures.end()) abort();

	nf.closure.elems[path] = j->second;

	for (PathSet::iterator k = j->second.refs.begin();
	     k != j->second.refs.end(); k++)
	    usedPaths.insert(*k);
    }

    /* For debugging, print out the referenced and unreferenced paths. */
    for (ClosureElems::iterator i = inClosures.begin();
         i != inClosures.end(); i++)
    {
        PathSet::iterator j = donePaths.find(i->first);
        if (j == donePaths.end())
            debug(format("NOT referenced: `%1%'") % i->first);
        else
            debug(format("referenced: `%1%'") % i->first);
    }

    /* Write the normal form.  This does not have to occur in the
       transaction below because writing terms is idem-potent. */
    ATerm nfTerm = unparseNixExpr(nf);
    msg(lvlVomit, format("normal form: %1%") % printTerm(nfTerm));
    Path nfPath = writeTerm(nfTerm, "-s");

    /* Register each outpat path, and register the normal form.  This
       is wrapped in one database transaction to ensure that if we
       crash, either everything is registered or nothing is.  This is
       for recoverability: unregistered paths in the store can be
       deleted arbitrarily, while registered paths can only be deleted
       by running the garbage collector. */
    Transaction txn;
    createStoreTransaction(txn);
    for (PathSet::iterator i = ne.derivation.outputs.begin(); 
         i != ne.derivation.outputs.end(); i++)
        registerValidPath(txn, *i);
    registerSuccessor(txn, nePath, nfPath);
    txn.commit();

    return nfPath;
}


void realiseClosure(const Path & nePath, PathSet pending)
{
    Nest nest(lvlDebug, format("realising closure `%1%'") % nePath);

    NixExpr ne = exprFromPath(nePath, pending);
    if (ne.type != NixExpr::neClosure)
        throw Error(format("expected closure in `%1%'") % nePath);
    
    for (ClosureElems::const_iterator i = ne.closure.elems.begin();
         i != ne.closure.elems.end(); i++)
        ensurePath(i->first, pending);
}


void ensurePath(const Path & path, PathSet pending)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;
    
    /* Otherwise, try the substitutes. */
    Paths subPaths = querySubstitutes(path);

    for (Paths::iterator i = subPaths.begin(); 
         i != subPaths.end(); i++)
    {
        try {
            normaliseNixExpr(*i, pending);
            if (isValidPath(path)) return;
            throw Error(format("substitute failed to produce expected output path"));
        } catch (Error & e) {
            msg(lvlTalkative, 
                format("building of substitute `%1%' for `%2%' failed: %3%")
                % *i % path % e.what());
        }
    }

    throw Error(format("path `%1%' is required, "
        "but there are no (successful) substitutes") % path);
}


NixExpr exprFromPath(const Path & path, PathSet pending)
{
    ensurePath(path, pending);
    ATerm t = ATreadFromNamedFile(path.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % path);
    return parseNixExpr(t);
}


PathSet nixExprRoots(const Path & nePath)
{
    PathSet paths;

    NixExpr ne = exprFromPath(nePath);

    if (ne.type == NixExpr::neClosure)
        paths.insert(ne.closure.roots.begin(), ne.closure.roots.end());
    else if (ne.type == NixExpr::neDerivation)
        paths.insert(ne.derivation.outputs.begin(),
            ne.derivation.outputs.end());
    else abort();

    return paths;
}


static void requisitesWorker(const Path & nePath,
    bool includeExprs, bool includeSuccessors,
    PathSet & paths, PathSet & doneSet)
{
    if (doneSet.find(nePath) != doneSet.end()) return;
    doneSet.insert(nePath);

    NixExpr ne = exprFromPath(nePath);

    if (ne.type == NixExpr::neClosure)
        for (ClosureElems::iterator i = ne.closure.elems.begin();
             i != ne.closure.elems.end(); i++)
            paths.insert(i->first);
    
    else if (ne.type == NixExpr::neDerivation)
        for (PathSet::iterator i = ne.derivation.inputs.begin();
             i != ne.derivation.inputs.end(); i++)
            requisitesWorker(*i,
                includeExprs, includeSuccessors, paths, doneSet);

    else abort();

    if (includeExprs) paths.insert(nePath);

    string nfPath;
    if (includeSuccessors && (nfPath = useSuccessor(nePath)) != nePath)
        requisitesWorker(nfPath, includeExprs, includeSuccessors,
            paths, doneSet);
}


PathSet nixExprRequisites(const Path & nePath,
    bool includeExprs, bool includeSuccessors)
{
    PathSet paths;
    PathSet doneSet;
    requisitesWorker(nePath, includeExprs, includeSuccessors,
        paths, doneSet);
    return paths;
}
