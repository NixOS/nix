#include <map>

#include "normalise.hh"
#include "references.hh"
#include "db.hh"
#include "exec.hh"
#include "pathlocks.hh"
#include "globals.hh"


void registerSuccessor(const FSId & id1, const FSId & id2)
{
    Transaction txn(nixDB);
    nixDB.setString(txn, dbSuccessors, id1, id2);
    txn.commit();
}


static FSId storeSuccessor(const FSId & id1, ATerm sc)
{
    FSId id2 = writeTerm(sc, "-s-" + (string) id1);
    registerSuccessor(id1, id2);
    return id2;
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
    Strings refPaths;

    /* The environment to be passed to the builder. */
    Environment env; 


    /* Parse the outputs. */
    for (DeriveOutputs::iterator i = fs.derive.outputs.begin();
         i != fs.derive.outputs.end(); i++)
    {
        debug(format("building %1% in `%2%'") % (string) i->second % i->first);
        outPaths[i->first] = i->second;
        refPaths.push_back(i->first);
    }

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes. */
    PathLocks outputLock(pathsFromOutPaths(outPaths));

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
        refPaths.push_back(i->second.path);

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

        /* Check that none of the outputs exist. */
        for (OutPaths::iterator i = outPaths.begin(); 
             i != outPaths.end(); i++)
            if (pathExists(i->first))
                throw Error(format("path `%1%' exists") % i->first);

        /* Run the builder. */
        msg(lvlChatty, format("building..."));
        runProgram(fs.derive.builder, env);
        msg(lvlChatty, format("build completed"));
        
    } else
        msg(lvlChatty, format("fast build succesful"));

    /* Check whether the output paths were created, and register each
       one. */
    FSIdSet used;
    for (OutPaths::iterator i = outPaths.begin(); 
         i != outPaths.end(); i++)
    {
        string path = i->first;
        if (!pathExists(path))
            throw Error(format("path `%1%' does not exist") % path);
        registerPath(path, i->second);
        fs.slice.roots.push_back(i->second);

        Strings refs = filterReferences(path, refPaths);

        SliceElem elem;
        elem.path = path;
        elem.id = i->second;

        for (Strings::iterator j = refs.begin(); j != refs.end(); j++) {
            ElemMap::iterator k;
            OutPaths::iterator l;
            if ((k = inMap.find(*j)) != inMap.end()) {
                elem.refs.push_back(k->second.id);
                used.insert(k->second.id);
                for (FSIds::iterator m = k->second.refs.begin();
                     m != k->second.refs.end(); m++)
                    used.insert(*m);
            } else if ((l = outPaths.find(*j)) != outPaths.end()) {
                elem.refs.push_back(l->second);
                used.insert(l->second);
            } else 
                throw Error(format("unknown referenced path `%1%'") % *j);
        }

        fs.slice.elems.push_back(elem);
    }

    for (ElemMap::iterator i = inMap.begin();
         i != inMap.end(); i++)
    {
        FSIdSet::iterator j = used.find(i->second.id);
        if (j == used.end())
            debug(format("NOT referenced: `%1%'") % i->second.path);
        else {
            debug(format("referenced: `%1%'") % i->second.path);
            fs.slice.elems.push_back(i->second);
        }
    }

    fs.type = FState::fsSlice;
    ATerm nf = unparseFState(fs);
    msg(lvlVomit, format("normal form: %1%") % printTerm(nf));
    return storeSuccessor(id, nf);
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
