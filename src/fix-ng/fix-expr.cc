#include "fix-expr.hh"
#include "expr.hh"


ATermMap::ATermMap(unsigned int initialSize, unsigned int maxLoadPct)
{
    table = ATtableCreate(initialSize, maxLoadPct);
    if (!table) throw Error("cannot create ATerm table");
}


ATermMap::ATermMap(const ATermMap & map)
    : table(0)
{
    ATermList keys = map.keys();

    /* !!! adjust allocation for load pct */
    table = ATtableCreate(ATgetLength(keys), map.maxLoadPct);
    if (!table) throw Error("cannot create ATerm table");

    for (; !ATisEmpty(keys); keys = ATgetNext(keys)) {
        ATerm key = ATgetFirst(keys);
        set(key, map.get(key));
    }
}


ATermMap::~ATermMap()
{
    if (table) ATtableDestroy(table);
}


void ATermMap::set(ATerm key, ATerm value)
{
    return ATtablePut(table, key, value);
}


void ATermMap::set(const string & key, ATerm value)
{
    set(string2ATerm(key), value);
}


ATerm ATermMap::get(ATerm key) const
{
    return ATtableGet(table, key);
}


ATerm ATermMap::get(const string & key) const
{
    return get(string2ATerm(key));
}


void ATermMap::remove(ATerm key)
{
    ATtableRemove(table, key);
}


void ATermMap::remove(const string & key)
{
    remove(string2ATerm(key));
}


ATermList ATermMap::keys() const
{
    ATermList keys = ATtableKeys(table);
    if (!keys) throw Error("cannot query aterm map keys");
    return keys;
}


ATerm string2ATerm(const string & s)
{
    return (ATerm) ATmakeAppl0(ATmakeAFun((char *) s.c_str(), 0, ATtrue));
}


string aterm2String(ATerm t)
{
    return ATgetName(ATgetAFun(t));
}
    

ATerm bottomupRewrite(TermFun & f, ATerm e)
{
    if (ATgetType(e) == AT_APPL) {
        AFun fun = ATgetAFun(e);
        int arity = ATgetArity(fun);
        ATermList args = ATempty;

        for (int i = arity - 1; i >= 0; i--)
            args = ATinsert(args, bottomupRewrite(f, ATgetArgument(e, i)));
        
        e = (ATerm) ATmakeApplList(fun, args);
    }

    else if (ATgetType(e) == AT_LIST) {
        ATermList in = (ATermList) e;
        ATermList out = ATempty;

        while (!ATisEmpty(in)) {
            out = ATinsert(out, bottomupRewrite(f, ATgetFirst(in)));
            in = ATgetNext(in);
        }

        e = (ATerm) ATreverse(out);
    }

    return f(e);
}


void queryAllAttrs(Expr e, ATermMap & attrs)
{
    ATermList bnds;
    if (!ATmatch(e, "Attrs([<list>])", &bnds))
        throw badTerm("expected attribute set", e);

    while (!ATisEmpty(bnds)) {
        char * s;
        Expr e;
        if (!ATmatch(ATgetFirst(bnds), "Bind(<str>, <term>)", &s, &e))
            abort(); /* can't happen */
        attrs.set(s, e);
        bnds = ATgetNext(bnds);
    }
}


Expr queryAttr(Expr e, const string & name)
{
    ATermMap attrs;
    queryAllAttrs(e, attrs);
    return attrs.get(name);
}


Expr makeAttrs(const ATermMap & attrs)
{
    ATermList bnds = ATempty, keys = attrs.keys();
    while (!ATisEmpty(keys)) {
        Expr key = ATgetFirst(keys);
        bnds = ATinsert(bnds, 
            ATmake("Bind(<term>, <term>)", key, attrs.get(key)));
        keys = ATgetNext(keys);
    }
    return ATmake("Attrs(<term>)", ATreverse(bnds));
}


Expr substitute(const ATermMap & subs, Expr e)
{
    char * s;

    if (ATmatch(e, "Var(<str>)", &s)) {
        Expr sub = subs.get(s);
        return sub ? sub : e;
    }

    /* In case of a function, filter out all variables bound by this
       function. */
    ATermList formals;
    ATerm body;
    if (ATmatch(e, "Function([<list>], <term>)", &formals, &body)) {
        ATermMap subs2(subs);
        ATermList fs = formals;
        while (!ATisEmpty(fs)) {
            if (!ATmatch(ATgetFirst(fs), "<str>", &s)) abort();
            subs2.remove(s);
            fs = ATgetNext(fs);
        }
        return ATmake("Function(<term>, <term>)", formals,
            substitute(subs2, body));
    }

    /* Idem for a mutually recursive attribute set. */
    ATermList bindings;
    if (ATmatch(e, "Rec([<list>])", &bindings)) {
        ATermMap subs2(subs);
        ATermList bnds = bindings;
        while (!ATisEmpty(bnds)) {
            Expr e;
            if (!ATmatch(ATgetFirst(bnds), "Bind(<str>, <term>)", &s, &e))
                abort(); /* can't happen */
            subs2.remove(s);
            bnds = ATgetNext(bnds);
        }
        return ATmake("Rec(<term>)", substitute(subs2, (ATerm) bindings));
    }

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
