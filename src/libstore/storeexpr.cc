#include "storeexpr.hh"
#include "globals.hh"
#include "store.hh"

#include "storeexpr-ast.hh"
#include "storeexpr-ast.cc"


Hash hashTerm(ATerm t)
{
    return hashString(htSHA256, atPrint(t));
}


Path writeTerm(ATerm t, const string & suffix)
{
    char * s = ATwriteToString(t);
    if (!s) throw Error("cannot print aterm");
    return addTextToStore(suffix + ".store", string(s));
}


void checkPath(const string & s)
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


static void checkClosure(const Closure & closure)
{
    if (closure.elems.size() == 0)
        throw Error("empty closure");

    PathSet decl;
    for (ClosureElems::const_iterator i = closure.elems.begin();
         i != closure.elems.end(); i++)
        decl.insert(i->first);
    
    for (PathSet::const_iterator i = closure.roots.begin();
         i != closure.roots.end(); i++)
        if (decl.find(*i) == decl.end())
            throw Error(format("undefined root path `%1%'") % *i);
    
    for (ClosureElems::const_iterator i = closure.elems.begin();
         i != closure.elems.end(); i++)
        for (PathSet::const_iterator j = i->second.refs.begin();
             j != i->second.refs.end(); j++)
            if (decl.find(*j) == decl.end())
                throw Error(
		    format("undefined path `%1%' referenced by `%2%'")
		    % *j % i->first);
}


/* Parse a closure. */
static bool parseClosure(ATerm t, Closure & closure)
{
    ATermList roots, elems;

    if (!matchClosure(t, roots, elems))
        return false;

    parsePaths(roots, closure.roots);

    for (ATermIterator i(elems); i; ++i) {
        ATerm path;
        ATermList refs;
        if (!matchClosureElem(*i, path, refs))
            throw badTerm("not a closure element", *i);
        ClosureElem elem;
        parsePaths(refs, elem.refs);
        closure.elems[aterm2String(path)] = elem;
    }

    checkClosure(closure);
    return true;
}


static bool parseDerivation(ATerm t, Derivation & derivation)
{
    ATermList outs, ins, args, bnds;
    ATerm builder, platform;

    if (!matchDerive(t, outs, ins, platform, builder, args, bnds))
        return false;

    for (ATermIterator i(outs); i; ++i) {
        ATerm id, path, hashAlgo, hash;
        if (!matchDerivationOutput(*i, id, path, hashAlgo, hash))
            return false;
        DerivationOutput out;
        out.path = aterm2String(path);
        checkPath(out.path);
        out.hashAlgo = aterm2String(hashAlgo);
        out.hash = aterm2String(hash);
        derivation.outputs[aterm2String(id)] = out;
    }

    parsePaths(ins, derivation.inputs);

    derivation.builder = aterm2String(builder);
    derivation.platform = aterm2String(platform);
    
    for (ATermIterator i(args); i; ++i) {
        if (ATgetType(*i) != AT_APPL)
            throw badTerm("string expected", *i);
        derivation.args.push_back(aterm2String(*i));
    }

    for (ATermIterator i(bnds); i; ++i) {
        ATerm s1, s2;
        if (!matchEnvBinding(*i, s1, s2))
            throw badTerm("tuple of strings expected", *i);
        derivation.env[aterm2String(s1)] = aterm2String(s2);
    }

    return true;
}


StoreExpr parseStoreExpr(ATerm t)
{
    StoreExpr ne;
    if (parseClosure(t, ne.closure))
        ne.type = StoreExpr::neClosure;
    else if (parseDerivation(t, ne.derivation))
        ne.type = StoreExpr::neDerivation;
    else throw badTerm("not a store expression", t);
    return ne;
}


static ATermList unparsePaths(const PathSet & paths)
{
    ATermList l = ATempty;
    for (PathSet::const_iterator i = paths.begin();
         i != paths.end(); i++)
        l = ATinsert(l, toATerm(*i));
    return ATreverse(l);
}


static ATerm unparseClosure(const Closure & closure)
{
    ATermList roots = unparsePaths(closure.roots);
    
    ATermList elems = ATempty;
    for (ClosureElems::const_iterator i = closure.elems.begin();
         i != closure.elems.end(); i++)
        elems = ATinsert(elems,
            makeClosureElem(
                toATerm(i->first),
                unparsePaths(i->second.refs)));

    return makeClosure(roots, elems);
}


static ATerm unparseDerivation(const Derivation & derivation)
{
    ATermList outputs = ATempty;
    for (DerivationOutputs::const_iterator i = derivation.outputs.begin();
         i != derivation.outputs.end(); i++)
        outputs = ATinsert(outputs,
            makeDerivationOutput(
                toATerm(i->first),
                toATerm(i->second.path),
                toATerm(i->second.hashAlgo),
                toATerm(i->second.hash)));

    ATermList args = ATempty;
    for (Strings::const_iterator i = derivation.args.begin();
         i != derivation.args.end(); i++)
        args = ATinsert(args, toATerm(*i));

    ATermList env = ATempty;
    for (StringPairs::const_iterator i = derivation.env.begin();
         i != derivation.env.end(); i++)
        env = ATinsert(env,
            makeEnvBinding(
                toATerm(i->first),
                toATerm(i->second)));

    return makeDerive(
        ATreverse(outputs),
        unparsePaths(derivation.inputs),
        toATerm(derivation.platform),
        toATerm(derivation.builder),
        ATreverse(args),
        ATreverse(env));
}


ATerm unparseStoreExpr(const StoreExpr & ne)
{
    if (ne.type == StoreExpr::neClosure)
        return unparseClosure(ne.closure);
    else if (ne.type == StoreExpr::neDerivation)
        return unparseDerivation(ne.derivation);
    else abort();
}
