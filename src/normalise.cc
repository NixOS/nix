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


typedef map<string, FSId> OutPaths;
typedef map<string, SliceElem> ElemMap;


Strings pathsFromOutPaths(const OutPaths & ps)
{
    Strings ss;
    for (OutPaths::const_iterator i = ps.begin();
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

    /* Output paths, with their ids. */
    OutPaths outPaths;

    /* Input paths, with their slice elements. */
    ElemMap inMap; 

    /* Referencable paths (i.e., input and output paths). */
    Strings allPaths;

    /* The environment to be passed to the builder. */
    Environment env; 


    /* Parse the outputs. */
    for (DeriveOutputs::iterator i = fs.derive.outputs.begin();
         i != fs.derive.outputs.end(); i++)
    {
        debug(format("building %1% in `%2%'") % (string) i->second % i->first);
        outPaths[i->first] = i->second;
        allPaths.push_back(i->first);
    }

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes. */
    PathLocks outputLocks(pathsFromOutPaths(outPaths));

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
    for (FSIds::iterator i = fs.derive.inputs.begin();
         i != fs.derive.inputs.end(); i++) {
        FSId nf = normaliseFState(*i, pending);
        realiseSlice(nf, pending);
        /* !!! nf should be a root of the garbage collector while we
           are building */
        FState fs = parseFState(termFromId(nf));
        if (fs.type != FState::fsSlice) abort();
        for (SliceElems::iterator j = fs.slice.elems.begin();
             j != fs.slice.elems.end(); j++)
            inMap[j->path] = *j;
    }

    for (ElemMap::iterator i = inMap.begin(); i != inMap.end(); i++)
        allPaths.push_back(i->second.path);

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
    for (OutPaths::iterator i = outPaths.begin();
         i != outPaths.end(); i++)
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
        for (OutPaths::iterator i = outPaths.begin(); 
             i != outPaths.end(); i++)
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
    for (OutPaths::iterator i = outPaths.begin(); 
         i != outPaths.end(); i++)
    {
        string path = i->first;
        if (!pathExists(path))
            throw Error(format("path `%1%' does not exist") % path);
        fs.slice.roots.push_back(i->second);

	/* For this output path, find the references to other paths contained
	   in it. */
        Strings refPaths = filterReferences(path, allPaths);

	/* Construct a slice element for this output path. */
        SliceElem elem;
        elem.path = path;
        elem.id = i->second;

	/* For each path referenced by this output path, add its id to the
	   slice element and add the id to the `used' set (so that the
	   elements referenced by *its* slice are added below). */
        for (Strings::iterator j = refPaths.begin();
	     j != refPaths.end(); j++)
	{
	    string path = *j;
            ElemMap::iterator k;
            OutPaths::iterator l;

	    /* Is it an input path? */
            if ((k = inMap.find(path)) != inMap.end()) {
                elem.refs.push_back(k->second.id);
                usedPaths.insert(k->second.path);
            }

	    /* Or an output path? */
	    else if ((l = outPaths.find(path)) != outPaths.end())
                elem.refs.push_back(l->second);
            
	    /* Can't happen. */ 
	    else abort();
        }

        fs.slice.elems.push_back(elem);
    }

    /* Close the slice.  That is, for any referenced path, add the paths
       referenced by it. */
    FSIdSet donePaths;

    while (!usedPaths.empty()) {
	StringSet::iterator i = usedPaths.begin();
	string path = *i;
	usedPaths.erase(i);

	ElemMap::iterator j = inMap.find(path);
	if (j == inMap.end()) abort();

	donePaths.insert(j->second.id);

	fs.slice.elems.push_back(j->second);

	for (FSIds::iterator k = j->second.refs.begin();
	     k != j->second.refs.end(); k++)
	    if (donePaths.find(*k) == donePaths.end()) {
		/* !!! performance */
		bool found = false;
		for (ElemMap::iterator l = inMap.begin();
		     l != inMap.end(); l++)
		    if (l->second.id == *k) {
			usedPaths.insert(l->first);
			found = true;
		    }
		if (!found) abort();
	    }
    }

    /* For debugging, print out the referenced and unreferenced paths. */
    for (ElemMap::iterator i = inMap.begin();
         i != inMap.end(); i++)
    {
        FSIdSet::iterator j = donePaths.find(i->second.id);
        if (j == donePaths.end())
            debug(format("NOT referenced: `%1%'") % i->second.path);
        else
            debug(format("referenced: `%1%'") % i->second.path);
    }

    /* Write the normal form.  This does not have to occur in the
       transaction below because writing terms is idem-potent. */
    fs.type = FState::fsSlice;
    ATerm nf = unparseFState(fs);
    msg(lvlVomit, format("normal form: %1%") % printTerm(nf));
    FSId idNF = writeTerm(nf, "-s-" + (string) id);

    /* Register each outpat path, and register the normal form.  This
       is wrapped in one database transaction to ensure that if we
       crash, either everything is registered or nothing is.  This is
       for recoverability: unregistered paths in the store can be
       deleted arbitrarily, while registered paths can only be deleted
       by running the garbage collector. */
    Transaction txn(nixDB);
    for (OutPaths::iterator i = outPaths.begin(); 
         i != outPaths.end(); i++)
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
    {
        SliceElem elem = *i;
        expandId(elem.id, elem.path, "/", pending);
    }
}


Strings fstatePaths(const FSId & id)
{
    Strings paths;

    FState fs = parseFState(termFromId(id));

    if (fs.type == FState::fsSlice) {
        /* !!! fix complexity */
        for (FSIds::const_iterator i = fs.slice.roots.begin();
             i != fs.slice.roots.end(); i++)
            for (SliceElems::const_iterator j = fs.slice.elems.begin();
                 j != fs.slice.elems.end(); j++)
                if (*i == j->id) paths.push_back(j->path);
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

    if (fs.type == FState::fsSlice) {
        for (SliceElems::iterator i = fs.slice.elems.begin();
             i != fs.slice.elems.end(); i++)
            paths.insert(i->path);
    }
    
    else if (fs.type == FState::fsDerive) {
        for (FSIds::iterator i = fs.derive.inputs.begin();
             i != fs.derive.inputs.end(); i++)
            fstateRequisitesSet(*i,
                includeExprs, includeSuccessors, paths);
    }

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
            if (ids.find(i->id) == ids.end()) {
                okay = false;
                break;
            }
        
        if (!okay) continue;
        
        generators.push_back(id);
    }

    return generators;
}
