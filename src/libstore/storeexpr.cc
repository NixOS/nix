#include "storeexpr.hh"
#include "globals.hh"
#include "store.hh"

#include "storeexpr-ast.hh"
#include "storeexpr-ast.cc"


Hash hashTerm(ATerm t)
{
    return hashString(htSHA256, atPrint(t));
}


Path writeDerivation(const Derivation & drv, const string & name)
{
    return addTextToStore(name + drvExtension,
        atPrint(unparseDerivation(drv)));
}


static void checkPath(const string & s)
{
    if (s.size() == 0 || s[0] != '/')
        throw Error(format("bad path `%1%' in store expression") % s);
}
    

static void parsePaths(ATermList paths, PathSet & out)
{
    for (ATermIterator i(paths); i; ++i) {
        if (ATgetType(*i) != AT_APPL)
            throw badTerm("not a path", *i);
        string s = aterm2String(*i);
        checkPath(s);
        out.insert(s);
    }
}


void throwBadDrv(ATerm t)
{
    throw badTerm("not a valid derivation", t);
}


Derivation parseDerivation(ATerm t)
{
    Derivation drv;
    ATermList outs, inDrvs, inSrcs, args, bnds;
    ATerm builder, platform;

    if (!matchDerive(t, outs, inDrvs, inSrcs, platform, builder, args, bnds))
        throwBadDrv(t);

    for (ATermIterator i(outs); i; ++i) {
        ATerm id, path, hashAlgo, hash;
        if (!matchDerivationOutput(*i, id, path, hashAlgo, hash))
            throwBadDrv(t);
        DerivationOutput out;
        out.path = aterm2String(path);
        checkPath(out.path);
        out.hashAlgo = aterm2String(hashAlgo);
        out.hash = aterm2String(hash);
        drv.outputs[aterm2String(id)] = out;
    }

    parsePaths(inDrvs, drv.inputDrvs);
    parsePaths(inSrcs, drv.inputSrcs);

    drv.builder = aterm2String(builder);
    drv.platform = aterm2String(platform);
    
    for (ATermIterator i(args); i; ++i) {
        if (ATgetType(*i) != AT_APPL)
            throw badTerm("string expected", *i);
        drv.args.push_back(aterm2String(*i));
    }

    for (ATermIterator i(bnds); i; ++i) {
        ATerm s1, s2;
        if (!matchEnvBinding(*i, s1, s2))
            throw badTerm("tuple of strings expected", *i);
        drv.env[aterm2String(s1)] = aterm2String(s2);
    }

    return drv;
}


static ATermList unparsePaths(const PathSet & paths)
{
    ATermList l = ATempty;
    for (PathSet::const_iterator i = paths.begin();
         i != paths.end(); i++)
        l = ATinsert(l, toATerm(*i));
    return ATreverse(l);
}


ATerm unparseDerivation(const Derivation & drv)
{
    ATermList outputs = ATempty;
    for (DerivationOutputs::const_iterator i = drv.outputs.begin();
         i != drv.outputs.end(); i++)
        outputs = ATinsert(outputs,
            makeDerivationOutput(
                toATerm(i->first),
                toATerm(i->second.path),
                toATerm(i->second.hashAlgo),
                toATerm(i->second.hash)));

    ATermList args = ATempty;
    for (Strings::const_iterator i = drv.args.begin();
         i != drv.args.end(); i++)
        args = ATinsert(args, toATerm(*i));

    ATermList env = ATempty;
    for (StringPairs::const_iterator i = drv.env.begin();
         i != drv.env.end(); i++)
        env = ATinsert(env,
            makeEnvBinding(
                toATerm(i->first),
                toATerm(i->second)));

    return makeDerive(
        ATreverse(outputs),
        unparsePaths(drv.inputDrvs),
        unparsePaths(drv.inputSrcs),
        toATerm(drv.platform),
        toATerm(drv.builder),
        ATreverse(args),
        ATreverse(env));
}


bool isDerivation(const string & fileName)
{
    return
        fileName.size() >= drvExtension.size() &&
        string(fileName, fileName.size() - drvExtension.size()) == drvExtension;
}
