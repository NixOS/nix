#include <map>

#include "normalise.hh"
#include "references.hh"
#include "db.hh"
#include "exec.hh"
#include "globals.hh"


void registerSuccessor(const FSId & id1, const FSId & id2)
{
    setDB(nixDB, dbSuccessors, id1, id2);
}


static FSId storeSuccessor(const FSId & id1, ATerm sc)
{
    FSId id2 = writeTerm(sc, "-s-" + (string) id1);
    registerSuccessor(id1, id2);
    return id2;
}


typedef set<FSId> FSIdSet;


Slice normaliseFState(FSId id)
{
    debug(format("normalising fstate %1%") % (string) id);
    Nest nest(true);

    /* Try to substitute $id$ by any known successors in order to
       speed up the rewrite process. */
    string idSucc;
    while (queryDB(nixDB, dbSuccessors, id, idSucc)) {
        debug(format("successor %1% -> %2%") % (string) id % idSucc);
        id = parseHash(idSucc);
    }

    /* Get the fstate expression. */
    FState fs = parseFState(termFromId(id));

    /* It this is a normal form (i.e., a slice) we are done. */
    if (fs.type == FState::fsSlice) return fs.slice;
    
    /* Otherwise, it's a derivation. */

    /* Right platform? */
    if (fs.derive.platform != thisSystem)
        throw Error(format("a `%1%' is required, but I am a `%2%'")
            % fs.derive.platform % thisSystem);
        
    /* Realise inputs (and remember all input paths). */
    typedef map<string, SliceElem> ElemMap;

    ElemMap inMap;

    for (FSIds::iterator i = fs.derive.inputs.begin();
         i != fs.derive.inputs.end(); i++) {
        Slice slice = normaliseFState(*i);
        realiseSlice(slice);

        for (SliceElems::iterator j = slice.elems.begin();
             j != slice.elems.end(); j++)
            inMap[j->path] = *j;
    }

    Strings inPaths;
    for (ElemMap::iterator i = inMap.begin(); i != inMap.end(); i++)
        inPaths.push_back(i->second.path);

    /* Build the environment. */
    Environment env;
    for (StringPairs::iterator i = fs.derive.env.begin();
         i != fs.derive.env.end(); i++)
        env[i->first] = i->second;

    /* Parse the outputs. */
    typedef map<string, FSId> OutPaths;
    OutPaths outPaths;
    for (DeriveOutputs::iterator i = fs.derive.outputs.begin();
         i != fs.derive.outputs.end(); i++)
    {
        debug(format("building %1% in %2%") % (string) i->second % i->first);
        outPaths[i->first] = i->second;
        inPaths.push_back(i->first);
    }

    /* We can skip running the builder if we can expand all output
       paths from their ids. */
    bool fastBuild = true;
    for (OutPaths::iterator i = outPaths.begin(); 
         i != outPaths.end(); i++)
    {
        try {
            expandId(i->second, i->first);
        } catch (Error & e) {
            debug(format("fast build failed: %1%") % e.what());
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
        debug(format("building..."));
        runProgram(fs.derive.builder, env);
        debug(format("build completed"));
        
    } else
        debug(format("skipping build"));

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

        Strings refs = filterReferences(path, inPaths);

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
    debug(format("normal form: %1%") % printTerm(nf));
    storeSuccessor(id, nf);

    return fs.slice;
}


void realiseSlice(const Slice & slice)
{
    debug(format("realising slice"));
    Nest nest(true);

    /* Perhaps all paths already contain the right id? */

    bool missing = false;
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
    {
        SliceElem elem = *i;
        string id;
        if (!queryDB(nixDB, dbPath2Id, elem.path, id)) {
            if (pathExists(elem.path))
                throw Error(format("path `%1%' obstructed") % elem.path);
            missing = true;
            break;
        }
        if (parseHash(id) != elem.id)
            throw Error(format("path `%1%' obstructed") % elem.path);
    }

    if (!missing) {
        debug(format("already installed"));
        return;
    }

    /* For each element, expand its id at its path. */
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
    {
        SliceElem elem = *i;
        debug(format("expanding %1% in %2%") % (string) elem.id % elem.path);
        expandId(elem.id, elem.path);
    }
}


Strings fstatePaths(const FSId & id, bool normalise)
{
    Strings paths;

    FState fs;

    if (normalise) {
        fs.slice = normaliseFState(id);
        fs.type = FState::fsSlice;
    } else
        fs = parseFState(termFromId(id));

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


Strings fstateRefs(const FSId & id)
{
    Strings paths;
    Slice slice = normaliseFState(id);
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
        paths.push_back(i->path);
    return paths;
}


FSIds findGenerators(const FSIds & _ids)
{
    FSIdSet ids(_ids.begin(), _ids.end());
    FSIds generators;

    /* !!! hack; for performance, we just look at the rhs of successor
       mappings, since we know that those are Nix expressions. */

    Strings sucs;
    enumDB(nixDB, dbSuccessors, sucs);

    for (Strings::iterator i = sucs.begin();
         i != sucs.end(); i++)
    {
        string s;
        queryDB(nixDB, dbSuccessors, *i, s);
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
