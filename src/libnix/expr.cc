#include "expr.hh"
#include "globals.hh"
#include "store.hh"


string printTerm(ATerm t)
{
    char * s = ATwriteToString(t);
    if (!s) throw Error("cannot print term");
    return s;
}


Error badTerm(const format & f, ATerm t)
{
    char * s = ATwriteToString(t);
    if (!s) throw Error("cannot print term");
    if (strlen(s) > 1000) {
        int len;
        s = ATwriteToSharedString(t, &len);
        if (!s) throw Error("cannot print term");
    }
    return Error(format("%1%, in `%2%'") % f.str() % (string) s);
}


Hash hashTerm(ATerm t)
{
    return hashString(printTerm(t));
}


Path writeTerm(ATerm t, const string & suffix)
{
    /* The id of a term is its hash. */
    Hash h = hashTerm(t);

    Path path = canonPath(nixStore + "/" + 
        (string) h + suffix + ".nix");

    if (!isValidPath(path)) {
        char * s = ATwriteToString(t);
        if (!s) throw Error(format("cannot write aterm to `%1%'") % path);
        addTextToStore(path, string(s));
    }
    
    return path;
}


static void parsePaths(ATermList paths, PathSet & out)
{
    while (!ATisEmpty(paths)) {
        char * s;
        ATerm t = ATgetFirst(paths);
        if (!ATmatch(t, "<str>", &s))
            throw badTerm("not a path", t);
        out.insert(s);
        paths = ATgetNext(paths);
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
    
    if (!ATmatch(t, "Closure([<list>], [<list>])", &roots, &elems))
        return false;

    parsePaths(roots, closure.roots);

    while (!ATisEmpty(elems)) {
        char * s1;
        ATermList refs;
        ATerm t = ATgetFirst(elems);
        if (!ATmatch(t, "(<str>, [<list>])", &s1, &refs))
            throw badTerm("not a closure element", t);
        ClosureElem elem;
        parsePaths(refs, elem.refs);
        closure.elems[s1] = elem;
        elems = ATgetNext(elems);
    }

    checkClosure(closure);
    return true;
}


static bool parseDerivation(ATerm t, Derivation & derivation)
{
    ATermList outs, ins, args, bnds;
    char * builder;
    char * platform;

    if (!ATmatch(t, "Derive([<list>], [<list>], <str>, <str>, [<list>], [<list>])",
            &outs, &ins, &platform, &builder, &args, &bnds))
    {
        /* !!! compatibility -> remove eventually */
        if (!ATmatch(t, "Derive([<list>], [<list>], <str>, <str>, [<list>])",
                &outs, &ins, &builder, &platform, &bnds))
            return false;
        args = ATempty;
    }

    parsePaths(outs, derivation.outputs);
    parsePaths(ins, derivation.inputs);

    derivation.builder = builder;
    derivation.platform = platform;
    
    while (!ATisEmpty(args)) {
        char * s;
        ATerm arg = ATgetFirst(args);
        if (!ATmatch(arg, "<str>", &s))
            throw badTerm("string expected", arg);
        derivation.args.push_back(s);
        args = ATgetNext(args);
    }

    while (!ATisEmpty(bnds)) {
        char * s1, * s2;
        ATerm bnd = ATgetFirst(bnds);
        if (!ATmatch(bnd, "(<str>, <str>)", &s1, &s2))
            throw badTerm("tuple of strings expected", bnd);
        derivation.env[s1] = s2;
        bnds = ATgetNext(bnds);
    }

    return true;
}


NixExpr parseNixExpr(ATerm t)
{
    NixExpr ne;
    if (parseClosure(t, ne.closure))
        ne.type = NixExpr::neClosure;
    else if (parseDerivation(t, ne.derivation))
        ne.type = NixExpr::neDerivation;
    else throw badTerm("not a Nix expression", t);
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


ATerm unparseNixExpr(const NixExpr & ne)
{
    if (ne.type == NixExpr::neClosure)
        return unparseClosure(ne.closure);
    else if (ne.type == NixExpr::neDerivation)
        return unparseDerivation(ne.derivation);
    else abort();
}
