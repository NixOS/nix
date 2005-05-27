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
