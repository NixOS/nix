#include "build.hh"


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


 Path findOutput(const Derivation & drv, string id)
{
    for (DerivationOutputs::const_iterator i = drv.outputs.begin();
         i != drv.outputs.end(); ++i)
        if (i->first == id) return i->second.path;
    throw Error(format("derivation has no output `%1%'") % id);
}
