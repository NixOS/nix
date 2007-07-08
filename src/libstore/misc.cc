#include "misc.hh"
#include "store-api.hh"
#include "db.hh"

#include <aterm2.h>


namespace nix {


Derivation derivationFromPath(const Path & drvPath)
{
    assertStorePath(drvPath);
    //printMsg(lvlError, format("tttttttttttttttttt"));
    store->ensurePath(drvPath);
    //printMsg(lvlError, format("uuuuuuuuuuuuuuuuuu"));
    ATerm t = ATreadFromNamedFile(drvPath.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % drvPath);
    return parseDerivation(t);
}

void computeFSClosure(const Path & path, PathSet & paths, const bool & withComponents, const bool & withState, const int revision, bool flipDirection)
{
	PathSet allPaths;
	computeFSClosureRec(path, allPaths, revision, flipDirection);
	
	if(!withComponents && !withState)
		throw Error(format("Useless call to computeFSClosure, at leat withComponents or withState must be true"));
	
	//TODO MAYBE EDIT: HOW CAN THESE PATHS ALREADY BE VALID SOMETIMES ..... ?????????????????????
	for (PathSet::iterator i = allPaths.begin(); i != allPaths.end(); ++i)
		if ( !store->isValidPath(*i) && !store->isValidStatePath(*i) )
			throw Error(format("Not a state or store path: ") % *i);
	
    //if withState is false, we filter out all state paths
	if( withComponents && !withState ){
		for (PathSet::iterator i = allPaths.begin(); i != allPaths.end(); ++i)
			if ( store->isValidPath(*i) )
				paths.insert(*i);
	}
	//if withComponents is false, we filter out all component paths
	else if( !withComponents && withState ){
		for (PathSet::iterator i = allPaths.begin(); i != allPaths.end(); ++i)
			if ( store->isValidStatePath(*i) )
				paths.insert(*i);
	}
	//all
	else{
		paths = allPaths;	
	}
}

void computeFSClosureRec(const Path & path, PathSet & paths, const int revision, const bool & flipDirection)
{
    if (paths.find(path) != paths.end()) return;	//takes care of double entries
    
    paths.insert(path);

    PathSet references;
    PathSet stateReferences;
    
    if (flipDirection){
        store->queryReferrers(path, references, revision);
       	store->queryStateReferrers(path, stateReferences, revision);
    }
    else{
        store->queryReferences(path, references, revision);
       	store->queryStateReferences(path, stateReferences, revision);
    }

	PathSet allReferences;
	allReferences = pathSets_union(references, stateReferences);

    for (PathSet::iterator i = allReferences.begin(); i != allReferences.end(); ++i)
        computeFSClosureRec(*i, paths, revision, flipDirection);
}


Path findOutput(const Derivation & drv, string id)
{
    for (DerivationOutputs::const_iterator i = drv.outputs.begin();
         i != drv.outputs.end(); ++i)
        if (i->first == id) return i->second.path;
    throw Error(format("derivation has no output `%1%'") % id);
}


void queryMissing(const PathSet & targets,
    PathSet & willBuild, PathSet & willSubstitute)
{
    PathSet todo(targets.begin(), targets.end()), done;

    while (!todo.empty()) {
        Path p = *(todo.begin());
        todo.erase(p);
        if (done.find(p) != done.end()) continue;
        done.insert(p);

        if (isDerivation(p)) {
            if (!store->isValidPath(p)) continue;
            Derivation drv = derivationFromPath(p);

            bool mustBuild = false;
            for (DerivationOutputs::iterator i = drv.outputs.begin();
                 i != drv.outputs.end(); ++i)
                if (!store->isValidPath(i->second.path) &&
                    !store->hasSubstitutes(i->second.path))
                    mustBuild = true;

            if (mustBuild) {
                willBuild.insert(p);
                todo.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
                for (DerivationInputs::iterator i = drv.inputDrvs.begin();
                     i != drv.inputDrvs.end(); ++i)
                    todo.insert(i->first);
            } else 
                for (DerivationOutputs::iterator i = drv.outputs.begin();
                     i != drv.outputs.end(); ++i)
                    todo.insert(i->second.path);
        }

        else {
            if (store->isValidPath(p)) continue;
            if (store->hasSubstitutes(p))
                willSubstitute.insert(p);
            PathSet refs;
            store->queryReferences(p, todo, -1);		//TODO?
        }
    }
}

 
}
