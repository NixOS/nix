#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"
#include "aterm.hh"

#include "nixexpr-ast.hh"
#include "nixexpr-ast.cc"

#include <cstdlib>


namespace nix {
    

string showPos(ATerm pos)
{
    ATerm path;
    int line, column;
    if (matchNoPos(pos)) return "undefined position";
    if (!matchPos(pos, path, line, column))
        throw badTerm("position expected", pos);
    return (format("`%1%:%2%:%3%'") % aterm2String(path) % line % column).str();
}
    

ATerm bottomupRewrite(TermFun & f, ATerm e)
{
    checkInterrupt();

    if (ATgetType(e) == AT_APPL) {
        AFun fun = ATgetAFun(e);
        int arity = ATgetArity(fun);
        ATerm args[arity];

        for (int i = 0; i < arity; ++i)
            args[i] = bottomupRewrite(f, ATgetArgument(e, i));
        
        e = (ATerm) ATmakeApplArray(fun, args);
    }

    else if (ATgetType(e) == AT_LIST) {
        ATermList in = (ATermList) e;
        ATermList out = ATempty;

        for (ATermIterator i(in); i; ++i)
            out = ATinsert(out, bottomupRewrite(f, *i));

        e = (ATerm) ATreverse(out);
    }

    return f(e);
}


Expr makeAttrs(const ATermMap & attrs)
{
    ATermList bnds = ATempty;
    for (ATermMap::const_iterator i = attrs.begin(); i != attrs.end(); ++i) {
        Expr e;
        ATerm pos;
        if (!matchAttrRHS(i->value, e, pos))
            abort(); /* can't happen */
        bnds = ATinsert(bnds, makeBind(i->key, e, pos));
    }
    return makeAttrs(bnds);
}


static void varsBoundByPattern(ATermMap & map, Pattern pat)
{
    ATerm name;
    ATermList formals;
    ATermBool ellipsis;
    /* Use makeRemoved() so that it can be used directly in
       substitute(). */
    if (matchVarPat(pat, name))
        map.set(name, makeRemoved());
    else if (matchAttrsPat(pat, formals, ellipsis, name)) {
        if (name != sNoAlias) map.set(name, makeRemoved());
        for (ATermIterator i(formals); i; ++i) {
            ATerm d1;
            if (!matchFormal(*i, name, d1)) abort();
            map.set(name, makeRemoved());
        }
    }
    else abort();
}


/* We use memoisation to prevent exponential complexity on heavily
   shared ATerms (remember, an ATerm is a graph, not a tree!).  Note
   that using an STL set is fine here wrt to ATerm garbage collection
   since all the ATerms in the set are already reachable from
   somewhere else. */
static void checkVarDefs2(set<Expr> & done, const ATermMap & defs, Expr e)
{
    if (done.find(e) != done.end()) return;
    done.insert(e);
    
    ATerm name, pos, value;
    ATerm with, body;
    ATermList rbnds, nrbnds;
    Pattern pat;

    /* Closed terms don't have free variables, so we don't have to
       check by definition. */
    if (matchClosed(e, value)) return;
    
    else if (matchVar(e, name)) {
        if (!defs.get(name))
            throw EvalError(format("undefined variable `%1%'")
                % aterm2String(name));
    }

    else if (matchFunction(e, pat, body, pos)) {
        ATermMap defs2(defs);
        varsBoundByPattern(defs2, pat);
        set<Expr> done2;
        checkVarDefs2(done2, defs2, pat);
        checkVarDefs2(done2, defs2, body);
    }
        
    else if (matchRec(e, rbnds, nrbnds)) {
        checkVarDefs2(done, defs, (ATerm) nrbnds);
        ATermMap defs2(defs);
        for (ATermIterator i(rbnds); i; ++i) {
            if (!matchBind(*i, name, value, pos)) abort(); /* can't happen */
            defs2.set(name, (ATerm) ATempty);
        }
        for (ATermIterator i(nrbnds); i; ++i) {
            if (!matchBind(*i, name, value, pos)) abort(); /* can't happen */
            defs2.set(name, (ATerm) ATempty);
        }
        set<Expr> done2;
        checkVarDefs2(done2, defs2, (ATerm) rbnds);
    }

    else if (matchWith(e, with, body, pos)) {
        /* We can't check the body without evaluating the definitions
           (which is an arbitrary expression), so we don't do that
           here but only when actually evaluating the `with'. */
        checkVarDefs2(done, defs, with);
    }
    
    else if (ATgetType(e) == AT_APPL) {
        int arity = ATgetArity(ATgetAFun(e));
        for (int i = 0; i < arity; ++i)
            checkVarDefs2(done, defs, ATgetArgument(e, i));
    }

    else if (ATgetType(e) == AT_LIST)
        for (ATermIterator i((ATermList) e); i; ++i)
            checkVarDefs2(done, defs, *i);
}


void checkVarDefs(const ATermMap & defs, Expr e)
{
    set<Expr> done;
    checkVarDefs2(done, defs, e);
}


bool matchStr(Expr e, string & s, PathSet & context)
{
    ATermList l;
    ATerm s_;

    if (!matchStr(e, s_, l)) return false;

    s = aterm2String(s_);

    for (ATermIterator i(l); i; ++i)
        context.insert(aterm2String(*i));

    return true;
}


Expr makeStr(const string & s, const PathSet & context)
{
    return makeStr(toATerm(s), toATermList(context));
}


}
