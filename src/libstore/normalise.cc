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


Path normaliseStoreExpr(const Path & _nePath, PathSet pending)
{
    startNest(nest, lvlTalkative,
        format("normalising store expression in `%1%'") % (string) _nePath);

    /* Try to substitute the expression by any known successors in
       order to speed up the rewrite process. */
    Path nePath = useSuccessor(_nePath);

    /* Get the store expression. */
    StoreExpr ne = storeExprFromPath(nePath, pending);

    /* If this is a normal form (i.e., a closure) we are done. */
    if (ne.type == StoreExpr::neClosure) return nePath;
    if (ne.type != StoreExpr::neDerivation) abort();
    

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
    StoreExpr nf;
    nf.type = StoreExpr::neClosure;


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
            StoreExpr ne = storeExprFromPath(nePath2, pending);
            debug(format("skipping build of expression `%1%', someone beat us to it")
		  % (string) nePath);
            if (ne.type != StoreExpr::neClosure) abort();
            outputLocks.setDeletion(true);
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
        checkInterrupt();
        Path nfPath = normaliseStoreExpr(*i, pending);
        realiseClosure(nfPath, pending);
        /* !!! nfPath should be a root of the garbage collector while
           we are building */
        StoreExpr ne = storeExprFromPath(nfPath, pending);
        if (ne.type != StoreExpr::neClosure) abort();
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

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = nixStore;

    /* Build the environment. */
    for (StringPairs::iterator i = ne.derivation.env.begin();
         i != ne.derivation.env.end(); i++)
        env[i->first] = i->second;

    /* We can skip running the builder if all output paths are already
       valid. */
    bool fastBuild = true;
    for (PathSet::iterator i = ne.derivation.outputs.begin();
         i != ne.derivation.outputs.end(); i++)
    {
        if (!isValidPath(*i)) { 
            fastBuild = false;
            break;
        }
    }

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
        printMsg(lvlChatty, format("building..."));
        runProgram(ne.derivation.builder, ne.derivation.args, env,
            nixLogDir + "/" + baseNameOf(nePath));
        printMsg(lvlChatty, format("build completed"));
        
    } else
        printMsg(lvlChatty, format("fast build succesful"));

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
            checkInterrupt();
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
        checkInterrupt();
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
    ATerm nfTerm = unparseStoreExpr(nf);
    printMsg(lvlVomit, format("normal form: %1%") % atPrint(nfTerm));
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

    /* It is now safe to delete the lock files, since all future
       lockers will see the successor; they will not create new lock
       files with the same names as the old (unlinked) lock files. */
    outputLocks.setDeletion(true);

    return nfPath;
}


void realiseClosure(const Path & nePath, PathSet pending)
{
    startNest(nest, lvlDebug, format("realising closure `%1%'") % nePath);

    StoreExpr ne = storeExprFromPath(nePath, pending);
    if (ne.type != StoreExpr::neClosure)
        throw Error(format("expected closure in `%1%'") % nePath);
    
    for (ClosureElems::const_iterator i = ne.closure.elems.begin();
         i != ne.closure.elems.end(); i++)
        ensurePath(i->first, pending);
}


void ensurePath(const Path & path, PathSet pending)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    if (pending.find(path) != pending.end())
      throw Error(format(
          "path `%1%' already being realised (possible substitute cycle?)")
	  % path);
    pending.insert(path);
    
    /* Otherwise, try the substitutes. */
    Paths subPaths = querySubstitutes(path);

    for (Paths::iterator i = subPaths.begin(); 
         i != subPaths.end(); i++)
    {
        checkInterrupt();
        try {
            Path nf = normaliseStoreExpr(*i, pending);
	    realiseClosure(nf, pending);
            if (isValidPath(path)) return;
            throw Error(format("substitute failed to produce expected output path"));
        } catch (Error & e) {
            printMsg(lvlTalkative, 
                format("building of substitute `%1%' for `%2%' failed: %3%")
                % *i % path % e.what());
        }
    }

    throw Error(format("path `%1%' is required, "
        "but there are no (successful) substitutes") % path);
}


StoreExpr storeExprFromPath(const Path & path, PathSet pending)
{
    ensurePath(path, pending);
    ATerm t = ATreadFromNamedFile(path.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % path);
    return parseStoreExpr(t);
}


PathSet storeExprRoots(const Path & nePath)
{
    PathSet paths;

    StoreExpr ne = storeExprFromPath(nePath);

    if (ne.type == StoreExpr::neClosure)
        paths.insert(ne.closure.roots.begin(), ne.closure.roots.end());
    else if (ne.type == StoreExpr::neDerivation)
        paths.insert(ne.derivation.outputs.begin(),
            ne.derivation.outputs.end());
    else abort();

    return paths;
}


static void requisitesWorker(const Path & nePath,
    bool includeExprs, bool includeSuccessors,
    PathSet & paths, PathSet & doneSet)
{
    checkInterrupt();
    
    if (doneSet.find(nePath) != doneSet.end()) return;
    doneSet.insert(nePath);

    StoreExpr ne = storeExprFromPath(nePath);

    if (ne.type == StoreExpr::neClosure)
        for (ClosureElems::iterator i = ne.closure.elems.begin();
             i != ne.closure.elems.end(); i++)
            paths.insert(i->first);
    
    else if (ne.type == StoreExpr::neDerivation)
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


PathSet storeExprRequisites(const Path & nePath,
    bool includeExprs, bool includeSuccessors)
{
    PathSet paths;
    PathSet doneSet;
    requisitesWorker(nePath, includeExprs, includeSuccessors,
        paths, doneSet);
    return paths;
}
