#include "fix-expr.hh"
#include "expr.hh"


ATerm bottomupRewrite(TermFun & f, ATerm e)
{
    e = f(e);

    if (ATgetType(e) == AT_APPL) {
        AFun fun = ATgetAFun(e);
        int arity = ATgetArity(fun);
        ATermList args = ATempty;

        for (int i = arity - 1; i >= 0; i--)
            args = ATinsert(args, bottomupRewrite(f, ATgetArgument(e, i)));
        
        return (ATerm) ATmakeApplList(fun, args);
    }

    if (ATgetType(e) == AT_LIST) {
        ATermList in = (ATermList) e;
        ATermList out = ATempty;

        while (!ATisEmpty(in)) {
            out = ATinsert(out, bottomupRewrite(f, ATgetFirst(in)));
            in = ATgetNext(in);
        }

        return (ATerm) ATreverse(out);
    }

    return e;
}


void queryAllAttrs(Expr e, Attrs & attrs)
{
    ATermList bnds;
    if (!ATmatch(e, "Attrs([<list>])", &bnds))
        throw badTerm("expected attribute set", e);

    while (!ATisEmpty(bnds)) {
        char * s;
        Expr e;
        if (!ATmatch(ATgetFirst(bnds), "Bind(<str>, <term>)", &s, &e))
            abort(); /* can't happen */
        attrs[s] = e;
        bnds = ATgetNext(bnds);
    }
}


Expr queryAttr(Expr e, const string & name)
{
    Attrs attrs;
    queryAllAttrs(e, attrs);
    Attrs::iterator i = attrs.find(name);
    return i == attrs.end() ? 0 : i->second;
}


Expr makeAttrs(const Attrs & attrs)
{
    ATermList bnds = ATempty;
    for (Attrs::const_iterator i = attrs.begin(); i != attrs.end(); i++)
        bnds = ATinsert(bnds, 
            ATmake("Bind(<str>, <term>)", i->first.c_str(), i->second));
    return ATmake("Attrs(<term>)", ATreverse(bnds));
}


ATerm substitute(Subs & subs, ATerm e)
{
    char * s;

    if (ATmatch(e, "Var(<str>)", &s)) {
        Subs::iterator i = subs.find(s);
        if (i == subs.end())
            return e;
        else
            return i->second;
    }

    /* In case of a function, filter out all variables bound by this
       function. */
    ATermList formals;
    ATerm body;
    if (ATmatch(e, "Function([<list>], <term>)", &formals, &body)) {
        Subs subs2(subs);
        ATermList fs = formals;
        while (!ATisEmpty(fs)) {
            if (!ATmatch(ATgetFirst(fs), "<str>", &s)) abort();
            subs2.erase(s);
            fs = ATgetNext(fs);
        }
        return ATmake("Function(<term>, <term>)", formals,
            substitute(subs2, body));
    }

    /* !!! Rec(...) */

    if (ATgetType(e) == AT_APPL) {
        AFun fun = ATgetAFun(e);
        int arity = ATgetArity(fun);
        ATermList args = ATempty;

        for (int i = arity - 1; i >= 0; i--)
            args = ATinsert(args, substitute(subs, ATgetArgument(e, i)));
        
        return (ATerm) ATmakeApplList(fun, args);
    }

    if (ATgetType(e) == AT_LIST) {
        ATermList in = (ATermList) e;
        ATermList out = ATempty;

        while (!ATisEmpty(in)) {
            out = ATinsert(out, substitute(subs, ATgetFirst(in)));
            in = ATgetNext(in);
        }

        return (ATerm) ATreverse(out);
    }

    return e;
}
