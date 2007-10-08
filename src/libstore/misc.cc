#include "misc.hh"
#include "store-api.hh"
#include "db.hh"
#include "local-store.hh"

#include <aterm2.h>


namespace nix {


Derivation derivationFromPathPrivate(const bool dotxn, const Transaction & txn, const Path & drvPath)
{
    assertStorePath(drvPath);
    if(dotxn)
    	ensurePathTxn(txn, drvPath);
    else
    	store->ensurePath(drvPath);
    ATerm t = ATreadFromNamedFile(drvPath.c_str());
    if (!t) 
    	throw Error(format("cannot read aterm from `%1%'") % drvPath);
    return parseDerivation(t);
}

//Wrappers
Derivation derivationFromPath(const Path & drvPath)
{
	return derivationFromPathPrivate(false, noTxn, drvPath);
}
Derivation derivationFromPathTxn(const Transaction & txn, const Path & drvPath)
{
	return derivationFromPathPrivate(true, txn, drvPath);
}

void computeFSClosure(const Path & path, PathSet & paths, const bool & withComponents, const bool & withState, const int revision, bool flipDirection)
{
	computeFSClosureTxn(noTxn, path, paths, withComponents, withState, revision, flipDirection);
}

void computeFSClosureTxn(const Transaction & txn, const Path & path, PathSet & paths, const bool & withComponents, const bool & withState, const int revision, bool flipDirection)
{
	PathSet allPaths;
	computeFSClosureRecTxn(txn, path, allPaths, revision, flipDirection);
	
	if(!withComponents && !withState)
		throw Error(format("Useless call to computeFSClosure, at leat withComponents or withState must be true"));
	
	//TODO HOW CAN THESE PATHS ALREADY BE VALID SOMETIMES ..... ?????????????????????
	for (PathSet::iterator i = allPaths.begin(); i != allPaths.end(); ++i)
		if ( !isValidPathTxn(txn, *i) && !isValidStatePathTxn(txn, *i) )
			throw Error(format("Not a state or store path: ") % *i);
	
    //if withState is false, we filter out all state paths
	if( withComponents && !withState ){
		for (PathSet::iterator i = allPaths.begin(); i != allPaths.end(); ++i)
			if ( isValidPathTxn(txn, *i) )
				paths.insert(*i);
	}
	//if withComponents is false, we filter out all component paths
	else if( !withComponents && withState ){
		for (PathSet::iterator i = allPaths.begin(); i != allPaths.end(); ++i)
			if ( isValidStatePathTxn(txn, *i) )
				paths.insert(*i);
	}
	//all
	else{
		paths = allPaths;	
	}
}

void computeFSClosureRecTxn(const Transaction & txn, const Path & path, PathSet & paths, const int revision, const bool & flipDirection)
{
    if (paths.find(path) != paths.end())	//takes care of double entries 
    	return;

    //printMsg(lvlError, format("ComputeFSClosureRec '%1%'") % path);
    
    paths.insert(path);

    PathSet references;
    PathSet stateReferences;
    
    if (flipDirection){
        queryStoreReferrersTxn(txn, path, references, revision);
       	queryStateReferrersTxn(txn, path, stateReferences, revision);
    }
    else{
        queryXReferencesTxn(txn, path, references, true, revision);
       	queryXReferencesTxn(txn, path, stateReferences, false, revision);
    }

	PathSet allReferences;
	allReferences = pathSets_union(references, stateReferences);

    for (PathSet::iterator i = allReferences.begin(); i != allReferences.end(); ++i)
        computeFSClosureRecTxn(txn, *i, paths, revision, flipDirection);
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
            Derivation drv = derivationFromPathTxn(noTxn, p);

            bool mustBuild = false;
            for (DerivationOutputs::iterator i = drv.outputs.begin();
                 i != drv.outputs.end(); ++i)
                if (!store->isValidPath(i->second.path) && !store->hasSubstitutes(i->second.path))
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
            // XXX call the substituters
            // store->queryReferences(p, todo);
        }
    }
}

 
}
