#include "build.hh"
#include "misc.hh"


Derivation derivationFromPath(const Path & drvPath)
{
    assertStorePath(drvPath);
    ensurePath(drvPath);
    ATerm t = ATreadFromNamedFile(drvPath.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % drvPath);
    return parseDerivation(t);
}


void computeFSClosure(const Path & storePath,
    PathSet & paths, bool flipDirection)
{
    if (paths.find(storePath) != paths.end()) return;
    paths.insert(storePath);

    PathSet references;
    if (flipDirection)
        queryReferers(noTxn, storePath, references);
    else
        queryReferences(noTxn, storePath, references);

    for (PathSet::iterator i = references.begin();
         i != references.end(); ++i)
        computeFSClosure(*i, paths, flipDirection);
}


OutputEqClass findOutputEqClass(const Derivation & drv, const string & id)
{
    DerivationOutputs::const_iterator i = drv.outputs.find(id);
    if (i == drv.outputs.end())
        throw Error(format("derivation has no output `%1%'") % id);
    return i->second.eqClass;
}


Path findTrustedEqClassMember(const OutputEqClass & eqClass,
    const TrustId & trustId)
{
    OutputEqMembers members;
    queryOutputEqMembers(noTxn, eqClass, members);

    for (OutputEqMembers::iterator j = members.begin(); j != members.end(); ++j)
        if (j->trustId == trustId || j->trustId == "root") return j->path;

    return "";
}


typedef map<OutputEqClass, PathSet> ClassMap;
typedef map<OutputEqClass, Path> FinalClassMap;


static void findBestRewrite(const ClassMap::const_iterator & pos,
    const ClassMap::const_iterator & end,
    const PathSet & selection, const PathSet & unselection,
    unsigned int & bestCost, PathSet & bestSelection)
{
    if (pos != end) {
        for (PathSet::iterator i = pos->second.begin();
             i != pos->second.end(); ++i)
        {
            PathSet selection2(selection);
            selection2.insert(*i);
            
            PathSet unselection2(unselection);
            for (PathSet::iterator j = pos->second.begin();
                 j != pos->second.end(); ++j)
                if (i != j) unselection2.insert(*j);
            
            ClassMap::const_iterator j = pos; ++j;
            findBestRewrite(j, end, selection2, unselection2,
                bestCost, bestSelection);
        }
        return;
    }

    PathSet badPaths;
    for (PathSet::iterator i = selection.begin();
         i != selection.end(); ++i)
    {
        PathSet closure;
        computeFSClosure(*i, closure); 
        for (PathSet::iterator j = closure.begin();
             j != closure.end(); ++j)
            if (unselection.find(*j) != unselection.end())
                badPaths.insert(*i);
    }
    
    printMsg(lvlError, format("cost %1% %2%") % badPaths.size() % showPaths(badPaths));

    if (badPaths.size() < bestCost) {
        bestCost = badPaths.size();
        bestSelection = selection;
    }
}


static Path maybeRewrite(const Path & path, const PathSet & selection,
    const FinalClassMap & finalClassMap, const PathSet & sources,
    Replacements & replacements,
    unsigned int & nrRewrites)
{
    startNest(nest, lvlError, format("considering rewriting `%1%'") % path);

    assert(selection.find(path) != selection.end());

    if (replacements.find(path) != replacements.end()) return replacements[path];
    
    PathSet references;
    queryReferences(noTxn, path, references);

    HashRewrites rewrites;
    PathSet newReferences;
    
    for (PathSet::iterator i = references.begin(); i != references.end(); ++i) {

        if (*i == path || sources.find(*i) != sources.end()) {
            newReferences.insert(*i);
            continue; /* ignore self-references */
        }

        OutputEqClasses classes;
        queryOutputEqClasses(noTxn, *i, classes);
        assert(classes.size() > 0);

        FinalClassMap::const_iterator j = finalClassMap.find(*(classes.begin()));
        assert(j != finalClassMap.end());
        
        Path newPath = maybeRewrite(j->second, selection,
            finalClassMap, sources, replacements, nrRewrites);

        if (*i != newPath)
            rewrites[hashPartOf(*i)] = hashPartOf(newPath);

        references.insert(newPath);
    }

    if (rewrites.size() == 0) {
        replacements[path] = path;
        return path;
    }

    printMsg(lvlError, format("rewriting `%1%'") % path);

    Path newPath = addToStore(path,
        hashPartOf(path), namePartOf(path),
        references, rewrites);

    nrRewrites++;

    printMsg(lvlError, format("rewrote `%1%' to `%2%'") % path % newPath);

    replacements[path] = newPath;
        
    return newPath;
}


PathSet consolidatePaths(const PathSet & paths, bool checkOnly,
    Replacements & replacements)
{
    printMsg(lvlError, format("consolidating"));
    
    ClassMap classMap;
    PathSet sources;
    
    for (PathSet::const_iterator i = paths.begin(); i != paths.end(); ++i) {
        OutputEqClasses classes;
        queryOutputEqClasses(noTxn, *i, classes);

        if (classes.size() == 0)
            sources.insert(*i);
        else
            for (OutputEqClasses::iterator j = classes.begin(); j != classes.end(); ++j)
                classMap[*j].insert(*i);
    }

    printMsg(lvlError, format("found %1% sources") % sources.size());

    bool conflict = false;
    for (ClassMap::iterator i = classMap.begin(); i != classMap.end(); ++i)
        if (i->second.size() >= 2) {
            printMsg(lvlError, format("conflict in eq class `%1%'") % i->first);
            conflict = true;
        }

    if (!conflict) return paths;
    
    assert(!checkOnly);

    
    /* !!! exponential-time algorithm! */
    const unsigned int infinity = 1000000;
    unsigned int bestCost = infinity;
    PathSet bestSelection;
    findBestRewrite(classMap.begin(), classMap.end(),
        PathSet(), PathSet(), bestCost, bestSelection);

    assert(bestCost != infinity);

    printMsg(lvlError, format("cheapest selection %1% %2%")
        % bestCost % showPaths(bestSelection));

    FinalClassMap finalClassMap;
    for (ClassMap::iterator i = classMap.begin(); i != classMap.end(); ++i)
        for (PathSet::const_iterator j = i->second.begin(); j != i->second.end(); ++j)
            if (bestSelection.find(*j) != bestSelection.end())
                finalClassMap[i->first] = *j;

    PathSet newPaths;
    unsigned int nrRewrites = 0;
    replacements.clear();
    for (PathSet::iterator i = bestSelection.begin();
         i != bestSelection.end(); ++i)
        newPaths.insert(maybeRewrite(*i, bestSelection, finalClassMap,
                            sources, replacements, nrRewrites));

    newPaths.insert(sources.begin(), sources.end());

    assert(nrRewrites == bestCost);

    assert(newPaths.size() < paths.size());

    return newPaths;
}
