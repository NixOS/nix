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


Strings pathsFromOutputs(const DeriveOutputs & ps)
{
    Strings ss;
    for (DeriveOutputs::const_iterator i = ps.begin();
         i != ps.end(); i++)
        ss.push_back(i->first);
    return ss;
}


FSId normaliseFState(FSId id, FSIdSet pending)
{
    Nest nest(lvlTalkative, format("normalising fstate %1%") % (string) id);

    /* Try to substitute $id$ by any known successors in order to
       speed up the rewrite process. */
    id = useSuccessor(id);

    /* Get the fstate expression. */
    FState fs = parseFState(termFromId(id));

    /* If this is a normal form (i.e., a slice) we are done. */
    if (fs.type == FState::fsSlice) return id;
    if (fs.type != FState::fsDerive) abort();
    

    /* Otherwise, it's a derive expression, and we have to build it to
       determine its normal form. */


    /* Some variables. */

    /* Input paths, with their slice elements. */
    SliceElems inSlices; 

    /* Referencable paths (i.e., input and output paths). */
    StringSet allPaths;

    /* The environment to be passed to the builder. */
    Environment env; 

    /* The result. */
    FState nfFS;
    nfFS.type = FState::fsSlice;


    /* Parse the outputs. */
    for (DeriveOutputs::iterator i = fs.derive.outputs.begin();
         i != fs.derive.outputs.end(); i++)
    {
        debug(format("building %1% in `%2%'") % (string) i->second % i->first);
        allPaths.insert(i->first);
    }

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes. */
    PathLocks outputLocks(pathsFromOutputs(fs.derive.outputs));

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
            FState fs = parseFState(termFromId(id2));
            debug(format("skipping build of %1%, someone beat us to it")
		  % (string) id);
            if (fs.type != FState::fsSlice) abort();
            return id2;
        }
    }

    /* Right platform? */
    if (fs.derive.platform != thisSystem)
        throw Error(format("a `%1%' is required, but I am a `%2%'")
		    % fs.derive.platform % thisSystem);
        
    /* Realise inputs (and remember all input paths). */
    for (FSIdSet::iterator i = fs.derive.inputs.begin();
         i != fs.derive.inputs.end(); i++)
    {
        FSId nf = normaliseFState(*i, pending);
        realiseSlice(nf, pending);
        /* !!! nf should be a root of the garbage collector while we
           are building */
        FState fs = parseFState(termFromId(nf));
        if (fs.type != FState::fsSlice) abort();
        for (SliceElems::iterator j = fs.slice.elems.begin();
             j != fs.slice.elems.end(); j++)
	{
            inSlices[j->first] = j->second;
	    allPaths.insert(j->first);
	}
    }

    /* Most shells initialise PATH to some default (/bin:/usr/bin:...) when
       PATH is not set.  We don't want this, so we fill it in with some dummy
       value. */
    env["PATH"] = "/path-not-set";

    /* Build the environment. */
    for (StringPairs::iterator i = fs.derive.env.begin();
         i != fs.derive.env.end(); i++)
        env[i->first] = i->second;

    /* We can skip running the builder if we can expand all output
       paths from their ids. */
    bool fastBuild = true;
    for (DeriveOutputs::iterator i = fs.derive.outputs.begin();
         i != fs.derive.outputs.end(); i++)
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
        for (DeriveOutputs::iterator i = fs.derive.outputs.begin(); 
             i != fs.derive.outputs.end(); i++)
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
        runProgram(fs.derive.builder, fs.derive.args, env);
        msg(lvlChatty, format("build completed"));
        
    } else
        msg(lvlChatty, format("fast build succesful"));

    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references. */
    StringSet usedPaths;
    for (DeriveOutputs::iterator i = fs.derive.outputs.begin(); 
         i != fs.derive.outputs.end(); i++)
    {
        string path = i->first;
        if (!pathExists(path))
            throw Error(format("path `%1%' does not exist") % path);
        nfFS.slice.roots.insert(path);

	/* For this output path, find the references to other paths contained
	   in it. */
        Strings refPaths = filterReferences(path, 
	    Strings(allPaths.begin(), allPaths.end()));

	/* Construct a slice element for this output path. */
        SliceElem elem;
        elem.id = i->second;

	/* For each path referenced by this output path, add its id to the
	   slice element and add the id to the `usedPaths' set (so that the
	   elements referenced by *its* slice are added below). */
        for (Strings::iterator j = refPaths.begin();
	     j != refPaths.end(); j++)
	{
	    string path = *j;
	    elem.refs.insert(path);
            if (inSlices.find(path) != inSlices.end())
                usedPaths.insert(path);
	    else if (fs.derive.outputs.find(path) == fs.derive.outputs.end())
		abort();
        }

        nfFS.slice.elems[path] = elem;
    }

    /* Close the slice.  That is, for any referenced path, add the paths
       referenced by it. */
    StringSet donePaths;

    while (!usedPaths.empty()) {
	StringSet::iterator i = usedPaths.begin();
	string path = *i;
	usedPaths.erase(i);

	if (donePaths.find(path) != donePaths.end()) continue;
	donePaths.insert(path);

	SliceElems::iterator j = inSlices.find(path);
	if (j == inSlices.end()) abort();

	nfFS.slice.elems[path] = j->second;

	for (StringSet::iterator k = j->second.refs.begin();
	     k != j->second.refs.end(); k++)
	    usedPaths.insert(*k);
    }

    /* For debugging, print out the referenced and unreferenced paths. */
    for (SliceElems::iterator i = inSlices.begin();
         i != inSlices.end(); i++)
    {
        StringSet::iterator j = donePaths.find(i->first);
        if (j == donePaths.end())
            debug(format("NOT referenced: `%1%'") % i->first);
        else
            debug(format("referenced: `%1%'") % i->first);
    }

    /* Write the normal form.  This does not have to occur in the
       transaction below because writing terms is idem-potent. */
    ATerm nf = unparseFState(nfFS);
    msg(lvlVomit, format("normal form: %1%") % printTerm(nf));
    FSId idNF = writeTerm(nf, "-s-" + (string) id);

    /* Register each outpat path, and register the normal form.  This
       is wrapped in one database transaction to ensure that if we
       crash, either everything is registered or nothing is.  This is
       for recoverability: unregistered paths in the store can be
       deleted arbitrarily, while registered paths can only be deleted
       by running the garbage collector. */
    Transaction txn(nixDB);
    for (DeriveOutputs::iterator i = fs.derive.outputs.begin(); 
         i != fs.derive.outputs.end(); i++)
        registerPath(txn, i->first, i->second);
    registerSuccessor(txn, id, idNF);
    txn.commit();

    return idNF;
}


void realiseSlice(const FSId & id, FSIdSet pending)
{
    Nest nest(lvlDebug, 
        format("realising slice %1%") % (string) id);

    FState fs = parseFState(termFromId(id));
    if (fs.type != FState::fsSlice)
        throw Error(format("expected slice in %1%") % (string) id);
    
    for (SliceElems::const_iterator i = fs.slice.elems.begin();
         i != fs.slice.elems.end(); i++)
        expandId(i->second.id, i->first, "/", pending);
}


Strings fstatePaths(const FSId & id)
{
    Strings paths;

    FState fs = parseFState(termFromId(id));

    if (fs.type == FState::fsSlice) {
        for (StringSet::const_iterator i = fs.slice.roots.begin();
             i != fs.slice.roots.end(); i++)
	    paths.push_back(*i);
    }

    else if (fs.type == FState::fsDerive) {
        for (DeriveOutputs::iterator i = fs.derive.outputs.begin();
             i != fs.derive.outputs.end(); i++)
            paths.push_back(i->first);
    }
    
    else abort();

    return paths;
}


static void fstateRequisitesSet(const FSId & id, 
    bool includeExprs, bool includeSuccessors, StringSet & paths)
{
    FState fs = parseFState(termFromId(id));

    if (fs.type == FState::fsSlice)
        for (SliceElems::iterator i = fs.slice.elems.begin();
             i != fs.slice.elems.end(); i++)
            paths.insert(i->first);
    
    else if (fs.type == FState::fsDerive)
        for (FSIdSet::iterator i = fs.derive.inputs.begin();
             i != fs.derive.inputs.end(); i++)
            fstateRequisitesSet(*i,
                includeExprs, includeSuccessors, paths);

    else abort();

    if (includeExprs) 
        paths.insert(expandId(id));

    string idSucc;
    if (includeSuccessors &&
        nixDB.queryString(noTxn, dbSuccessors, id, idSucc))
        fstateRequisitesSet(parseHash(idSucc), 
            includeExprs, includeSuccessors, paths);
}


Strings fstateRequisites(const FSId & id,
    bool includeExprs, bool includeSuccessors)
{
    StringSet paths;
    fstateRequisitesSet(id, includeExprs, includeSuccessors, paths);
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

        FState fs;
        try {
            /* !!! should substitutes be used? */
            fs = parseFState(termFromId(id));
        } catch (...) { /* !!! only catch parse errors */
            continue;
        }

        if (fs.type != FState::fsSlice) continue;
        
        bool okay = true;
        for (SliceElems::const_iterator i = fs.slice.elems.begin();
             i != fs.slice.elems.end(); i++)
            if (ids.find(i->second.id) == ids.end()) {
                okay = false;
                break;
            }
        
        if (!okay) continue;
        
        generators.push_back(id);
    }

    return generators;
}
