#include "misc.hh"
#include "store-api.hh"
#include "db.hh"

#include <aterm2.h>


namespace nix {


Derivation derivationFromPath(const Path & drvPath)
{
    assertStorePath(drvPath);
    store->ensurePath(drvPath);
    ATerm t = ATreadFromNamedFile(drvPath.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % drvPath);
    return parseDerivation(t);
}

void computeFSClosure(const Path & path, PathSet & paths, const bool & withState, bool flipDirection)
{
	PathSet allPaths;
	computeFSClosureRec(path, allPaths, flipDirection);
	
    //if withState is false, we filter out all state paths
	if(withState == false){
		for (PathSet::iterator i = allPaths.begin(); i != allPaths.end(); ++i){
			if ( ! store->isValidStatePath(*i) ){
				paths.insert(*i);
			
				//TODO (OBSOLETE) CHECK TO SEE IF THERE WERE NO /NIX/STATE PATHS THAT ARENT VALID AT THIS POINT, REMOVE THIS IN THE FUTURE
				string test = "/nix/state";
				if((*i).substr(0, test.size()) == test)
					throw Error(format("THIS CANNOT HAPPEN ! computeFSClosure is called before the state path was valid...."));
			}
		}
	}
	else{
		paths = allPaths;	
	}
}

void computeFSClosureRec(const Path & path, PathSet & paths, const bool & flipDirection)
{
    if (paths.find(path) != paths.end()) return;	//takes care of double entries
    
    paths.insert(path);

    PathSet references;
    PathSet stateReferences;
    
    if (flipDirection){
        store->queryReferrers(path, references);
       	store->queryStateReferrers(path, stateReferences);
    }
    else{
        store->queryReferences(path, references);
       	store->queryStateReferences(path, stateReferences);
    }

	PathSet allReferences;
	allReferences = mergePathSets(references, stateReferences);

    for (PathSet::iterator i = allReferences.begin(); i != allReferences.end(); ++i)
        computeFSClosureRec(*i, paths, flipDirection);
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
            store->queryReferences(p, todo);
        }
    }
}

 
}
