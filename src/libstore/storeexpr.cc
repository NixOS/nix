#include "storeexpr.hh"
#include "globals.hh"
#include "store.hh"


Hash hashTerm(ATerm t)
{
    return hashString(atPrint(t));
}


Path writeTerm(ATerm t, const string & suffix)
{
    /* The id of a term is its hash. */
    Hash h = hashTerm(t);

    Path path = canonPath(nixStore + "/" + 
        (string) h + suffix + ".store");

    if (!isValidPath(path)) {
        char * s = ATwriteToString(t);
        if (!s) throw Error(format("cannot write aterm to `%1%'") % path);
        addTextToStore(path, string(s));
    }
    
    return path;
}


static void parsePaths(ATermList paths, PathSet & out)
{
    ATMatcher m;
    for (ATermIterator i(paths); i; ++i) {
        string s;
        if (!(atMatch(m, *i) >> s))
            throw badTerm("not a path", *i);
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
    ATMatcher m;

    if (!(atMatch(m, t) >> "Closure" >> roots >> elems))
        return false;

    parsePaths(roots, closure.roots);

    for (ATermIterator i(elems); i; ++i) {
        string path;
        ATermList refs;
        if (!(atMatch(m, *i) >> "" >> path >> refs))
            throw badTerm("not a closure element", *i);
        ClosureElem elem;
        parsePaths(refs, elem.refs);
        closure.elems[path] = elem;
    }

    checkClosure(closure);
    return true;
}


static bool parseDerivation(ATerm t, Derivation & derivation)
{
    ATMatcher m;
    ATermList outs, ins, args, bnds;
    string builder, platform;

    if (!(atMatch(m, t) >> "Derive" >> outs >> ins >> platform
            >> builder >> args >> bnds))
        return false;

    parsePaths(outs, derivation.outputs);
    parsePaths(ins, derivation.inputs);

    derivation.builder = builder;
    derivation.platform = platform;
    
    for (ATermIterator i(args); i; ++i) {
        string s;
        if (!(atMatch(m, *i) >> s))
            throw badTerm("string expected", *i);
        derivation.args.push_back(s);
    }

    for (ATermIterator i(bnds); i; ++i) {
        string s1, s2;
        if (!(atMatch(m, *i) >> "" >> s1 >> s2))
            throw badTerm("tuple of strings expected", *i);
        derivation.env[s1] = s2;
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
        l = ATinsert(l, ATmake("<str>", i->c_str()));
    return ATreverse(l);
}


static ATerm unparseClosure(const Closure & closure)
{
    ATermList roots = unparsePaths(closure.roots);
    
    ATermList elems = ATempty;
    for (ClosureElems::const_iterator i = closure.elems.begin();
         i != closure.elems.end(); i++)
        elems = ATinsert(elems,
            ATmake("(<str>, <term>)",
                i->first.c_str(),
                unparsePaths(i->second.refs)));

    return ATmake("Closure(<term>, <term>)", roots, elems);
}


static ATerm unparseDerivation(const Derivation & derivation)
{
    ATermList args = ATempty;
    for (Strings::const_iterator i = derivation.args.begin();
         i != derivation.args.end(); i++)
        args = ATinsert(args, ATmake("<str>", i->c_str()));

    ATermList env = ATempty;
    for (StringPairs::const_iterator i = derivation.env.begin();
         i != derivation.env.end(); i++)
        env = ATinsert(env,
            ATmake("(<str>, <str>)", 
                i->first.c_str(), i->second.c_str()));

    return ATmake("Derive(<term>, <term>, <str>, <str>, <term>, <term>)",
        unparsePaths(derivation.outputs),
        unparsePaths(derivation.inputs),
        derivation.platform.c_str(),
        derivation.builder.c_str(),
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
