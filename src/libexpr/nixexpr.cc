#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"

#include <cstdlib>


namespace nix {


std::ostream & operator << (std::ostream & str, Expr & e)
{
    e.show(str);
    return str;
}


void Expr::show(std::ostream & str)
{
    abort();
}

void ExprInt::show(std::ostream & str)
{
    str << n;
}

void ExprString::show(std::ostream & str)
{
    str << "\"" << s << "\""; // !!! escaping
}

void ExprPath::show(std::ostream & str)
{
    str << s;
}

void ExprVar::show(std::ostream & str)
{
    str << name;
}

void ExprSelect::show(std::ostream & str)
{
    str << "(" << *e << ")." << name;
}

void ExprOpHasAttr::show(std::ostream & str)
{
    str << "(" << *e << ") ? " << name;
}

void ExprAttrs::show(std::ostream & str)
{
    if (recursive) str << "rec ";
    str << "{ ";
    foreach (list<string>::iterator, i, inherited)
        str << "inherit " << *i << "; ";
    foreach (Attrs::iterator, i, attrs)
        str << i->first << " = " << *i->second << "; ";
    str << "}";
}

void ExprList::show(std::ostream & str)
{
    str << "[ ";
    foreach (vector<Expr *>::iterator, i, elems)
        str << "(" << **i << ") ";
    str << "]";
}

void ExprLambda::show(std::ostream & str)
{
    str << "(";
    if (matchAttrs) {
        str << "{ ";
        bool first = true;
        foreach (Formals::Formals_::iterator, i, formals->formals) {
            if (first) first = false; else str << ", ";
            str << i->name;
            if (i->def) str << " ? " << *i->def;
        }
        str << " }";
        if (arg != "") str << " @ ";
    }
    if (arg != "") str << arg;
    str << ": " << *body << ")";
}

void ExprWith::show(std::ostream & str)
{
    str << "with " << *attrs << "; " << *body;
}

void ExprIf::show(std::ostream & str)
{
    str << "if " << *cond << " then " << *then << " else " << *else_;
}

void ExprAssert::show(std::ostream & str)
{
    str << "assert " << *cond << "; " << *body;
}

void ExprOpNot::show(std::ostream & str)
{
    str << "! " << *e;
}

void ExprConcatStrings::show(std::ostream & str)
{
    bool first = true;
    foreach (vector<Expr *>::iterator, i, *es) {
        if (first) first = false; else str << " + ";
        str << **i;
    }
}


std::ostream & operator << (std::ostream & str, const Pos & pos)
{
    if (!pos.line)
        str << "undefined position";
    else
        str << (format("`%1%:%2%:%3%'") % pos.file % pos.line % pos.column).str();
    return str;
}
    

#if 0
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
#endif


}
