#include <map>

#include "normalise.hh"
#include "references.hh"
#include "db.hh"
#include "exec.hh"
#include "pathlocks.hh"
#include "globals.hh"


void registerSuccessor(const Transaction & txn,
    const FSId & id1, const FSId & id2)
{
    nixDB.setString(txn, dbSuccessors, id1, id2);
}


static FSId useSuccessor(const FSId & id)
{
    string idSucc;
    if (nixDB.queryString(noTxn, dbSuccessors, id, idSucc)) {
        debug(format("successor %1% -> %2%") % (string) id % idSucc);
        return parseHash(idSucc);
    } else
        return id;
}


Strings pathsFromOutputs(const DerivationOutputs & ps)
{
    Strings ss;
    for (DerivationOutputs::const_iterator i = ps.begin();
         i != ps.end(); i++)
        ss.push_back(i->first);
    return ss;
}


FSId normaliseNixExpr(FSId id, FSIdSet pending)
{
    Nest nest(lvlTalkative,
        format("normalising nix expression %1%") % (string) id);

    /* Try to substitute $id$ by any known successors in order to
       speed up the rewrite process. */
    id = useSuccessor(id);

    /* Get the Nix expression. */
    NixExpr ne = parseNixExpr(termFromId(id));

    /* If this is a normal form (i.e., a closure) we are done. */
    if (ne.type == NixExpr::neClosure) return id;
    if (ne.type != NixExpr::neDerivation) abort();
    

    /* Otherwise, it's a derivation expression, and we have to build it to
       determine its normal form. */


    /* Some variables. */

    /* Input paths, with their closure elements. */
    ClosureElems inClosures; 

    /* Referencable paths (i.e., input and output paths). */
    StringSet allPaths;

    /* The environment to be passed to the builder. */
    Environment env; 

    /* The result. */
    NixExpr nf;
    nf.type = NixExpr::neClosure;


    /* Parse the outputs. */
    for (DerivationOutputs::iterator i = ne.derivation.outputs.begin();
         i != ne.derivation.outputs.end(); i++)
    {
        debug(format("building %1% in `%2%'") % (string) i->second % i->first);
        allPaths.insert(i->first);
    }

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes. */
    PathLocks outputLocks(pathsFromOutputs(ne.derivation.outputs));

    /* Now check again whether there is a successor.  This is because
       another process may have started building in parallel.  After
       it has finished and released the locks, we can (and should)
       reuse its results.  (Strictly speaking the first successor
       check above can be omitted, but that would be less efficient.)
       Note that since we now hold the locks on the output paths, no
       other process can build this expression, so no further checks
       are necessary. */
    {
        FSId id2 = useSuccessor(id);
        if (id2 != id) {
            NixExpr ne = parseNixExpr(termFromId(id2));
            debug(format("skipping build of %1%, someone beat us to it")
		  % (string) id);
            if (ne.type != NixExpr::neClosure) abort();
            return id2;
        }
    }

    /* Right platform? */
    if (ne.derivation.platform != thisSystem)
        throw Error(format("a `%1%' is required, but I am a `%2%'")
		    % ne.derivation.platform % thisSystem);
        
    /* Realise inputs (and remember all input paths). */
    for (FSIdSet::iterator i = ne.derivation.inputs.begin();
         i != ne.derivation.inputs.end(); i++)
    {
        FSId nf = normaliseNixExpr(*i, pending);
        realiseClosure(nf, pending);
        /* !!! nf should be a root of the garbage collector while we
           are building */
        NixExpr ne = parseNixExpr(termFromId(nf));
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
    bool fastBuild = true;
    for (DerivationOutputs::iterator i = ne.derivation.outputs.begin();
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

    if (!fastBuild) {

        /* If any of the outputs already exist but are not registered,
           delete them. */
        for (DerivationOutputs::iterator i = ne.derivation.outputs.begin(); 
             i != ne.derivation.outputs.end(); i++)
        {
            string path = i->first;
            FSId id;
            if (queryPathId(path, id))
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
    StringSet usedPaths;
    for (DerivationOutputs::iterator i = ne.derivation.outputs.begin(); 
         i != ne.derivation.outputs.end(); i++)
    {
        string path = i->first;
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
        elem.id = i->second;

	/* For each path referenced by this output path, add its id to the
	   closure element and add the id to the `usedPaths' set (so that the
	   elements referenced by *its* closure are added below). */
        for (Strings::iterator j = refPaths.begin();
	     j != refPaths.end(); j++)
	{
	    string path = *j;
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
    StringSet donePaths;

    while (!usedPaths.empty()) {
	StringSet::iterator i = usedPaths.begin();
	string path = *i;
	usedPaths.erase(i);

	if (donePaths.find(path) != donePaths.end()) continue;
	donePaths.insert(path);

	ClosureElems::iterator j = inClosures.find(path);
	if (j == inClosures.end()) abort();

	nf.closure.elems[path] = j->second;

	for (StringSet::iterator k = j->second.refs.begin();
	     k != j->second.refs.end(); k++)
	    usedPaths.insert(*k);
    }

    /* For debugging, print out the referenced and unreferenced paths. */
    for (ClosureElems::iterator i = inClosures.begin();
         i != inClosures.end(); i++)
    {
        StringSet::iterator j = donePaths.find(i->first);
        if (j == donePaths.end())
            debug(format("NOT referenced: `%1%'") % i->first);
        else
            debug(format("referenced: `%1%'") % i->first);
    }

    /* Write the normal form.  This does not have to occur in the
       transaction below because writing terms is idem-potent. */
    ATerm nfTerm = unparseNixExpr(nf);
    msg(lvlVomit, format("normal form: %1%") % printTerm(nfTerm));
    FSId idNF = writeTerm(nfTerm, "-s-" + (string) id);

    /* Register each outpat path, and register the normal form.  This
       is wrapped in one database transaction to ensure that if we
       crash, either everything is registered or nothing is.  This is
       for recoverability: unregistered paths in the store can be
       deleted arbitrarily, while registered paths can only be deleted
       by running the garbage collector. */
    Transaction txn(nixDB);
    for (DerivationOutputs::iterator i = ne.derivation.outputs.begin(); 
         i != ne.derivation.outputs.end(); i++)
        registerPath(txn, i->first, i->second);
    registerSuccessor(txn, id, idNF);
    txn.commit();

    return idNF;
}


void realiseClosure(const FSId & id, FSIdSet pending)
{
    Nest nest(lvlDebug, 
        format("realising closure %1%") % (string) id);

    NixExpr ne = parseNixExpr(termFromId(id));
    if (ne.type != NixExpr::neClosure)
        throw Error(format("expected closure in %1%") % (string) id);
    
    for (ClosureElems::const_iterator i = ne.closure.elems.begin();
         i != ne.closure.elems.end(); i++)
        expandId(i->second.id, i->first, "/", pending);
}


Strings nixExprPaths(const FSId & id)
{
    Strings paths;

    NixExpr ne = parseNixExpr(termFromId(id));

    if (ne.type == NixExpr::neClosure) {
        for (StringSet::const_iterator i = ne.closure.roots.begin();
             i != ne.closure.roots.end(); i++)
	    paths.push_back(*i);
    }

    else if (ne.type == NixExpr::neDerivation) {
        for (DerivationOutputs::iterator i = ne.derivation.outputs.begin();
             i != ne.derivation.outputs.end(); i++)
            paths.push_back(i->first);
    }
    
    else abort();

    return paths;
}


static void nixExprRequisitesSet(const FSId & id, 
    bool includeExprs, bool includeSuccessors, StringSet & paths,
    FSIdSet & doneSet)
{
    if (doneSet.find(id) != doneSet.end()) return;
    doneSet.insert(id);

    NixExpr ne = parseNixExpr(termFromId(id));

    if (ne.type == NixExpr::neClosure)
        for (ClosureElems::iterator i = ne.closure.elems.begin();
             i != ne.closure.elems.end(); i++)
            paths.insert(i->first);
    
    else if (ne.type == NixExpr::neDerivation)
        for (FSIdSet::iterator i = ne.derivation.inputs.begin();
             i != ne.derivation.inputs.end(); i++)
            nixExprRequisitesSet(*i,
                includeExprs, includeSuccessors, paths, doneSet);

    else abort();

    if (includeExprs) 
        paths.insert(expandId(id));

    string idSucc;
    if (includeSuccessors &&
        nixDB.queryString(noTxn, dbSuccessors, id, idSucc))
        nixExprRequisitesSet(parseHash(idSucc), 
            includeExprs, includeSuccessors, paths, doneSet);
}


Strings nixExprRequisites(const FSId & id,
    bool includeExprs, bool includeSuccessors)
{
    StringSet paths;
    FSIdSet doneSet;
    nixExprRequisitesSet(id, includeExprs, includeSuccessors, paths, doneSet);
    return Strings(paths.begin(), paths.end());
}


FSIds findGenerators(const FSIds & _ids)
{
    FSIdSet ids(_ids.begin(), _ids.end());
    FSIds generators;

    /* !!! hack; for performance, we just look at the rhs of successor
       mappings, since we know that those are Nix expressions. */

    Strings sucs;
    nixDB.enumTable(noTxn, dbSuccessors, sucs);

    for (Strings::iterator i = sucs.begin();
         i != sucs.end(); i++)
    {
        string s;
        if (!nixDB.queryString(noTxn, dbSuccessors, *i, s)) continue;
        FSId id = parseHash(s);

        NixExpr ne;
        try {
            /* !!! should substitutes be used? */
            ne = parseNixExpr(termFromId(id));
        } catch (...) { /* !!! only catch parse errors */
            continue;
        }

        if (ne.type != NixExpr::neClosure) continue;
        
        bool okay = true;
        for (ClosureElems::const_iterator i = ne.closure.elems.begin();
             i != ne.closure.elems.end(); i++)
            if (ids.find(i->second.id) == ids.end()) {
                okay = false;
                break;
            }
        
        if (!okay) continue;
        
        generators.push_back(id);
    }

    return generators;
}
