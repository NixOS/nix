#include "nixexpr.hh"
#include "storeexpr.hh"


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

    for (ATermIterator i(keys); i; ++i)
        set(*i, map.get(*i));
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
    checkInterrupt();

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

        for (ATermIterator i(in); i; ++i)
            out = ATinsert(out, bottomupRewrite(f, *i));

        e = (ATerm) ATreverse(out);
    }

    return f(e);
}


void queryAllAttrs(Expr e, ATermMap & attrs)
{
    ATMatcher m;
    ATermList bnds;
    if (!(atMatch(m, e) >> "Attrs" >> bnds))
        throw badTerm("expected attribute set", e);

    for (ATermIterator i(bnds); i; ++i) {
        string s;
        Expr e;
        if (!(atMatch(m, *i) >> "Bind" >> s >> e))
            abort(); /* can't happen */
        attrs.set(s, e);
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
    ATermList bnds = ATempty;
    for (ATermIterator i(attrs.keys()); i; ++i)
        bnds = ATinsert(bnds, 
            ATmake("Bind(<term>, <term>)", *i, attrs.get(*i)));
    return ATmake("Attrs(<term>)", ATreverse(bnds));
}


Expr substitute(const ATermMap & subs, Expr e)
{
    checkInterrupt();

    ATMatcher m;
    string s;

    if (atMatch(m, e) >> "Var" >> s) {
        Expr sub = subs.get(s);
        return sub ? sub : e;
    }

    /* In case of a function, filter out all variables bound by this
       function. */
    ATermList formals;
    ATerm body;
    if (atMatch(m, e) >> "Function" >> formals >> body) {
        ATermMap subs2(subs);
        for (ATermIterator i(formals); i; ++i) {
            Expr def;
            if (!(atMatch(m, *i) >> "NoDefFormal" >> s) &&
                !(atMatch(m, *i) >> "DefFormal" >> s >> def))
                abort();
            subs2.remove(s);
        }
        return ATmake("Function(<term>, <term>)", formals,
            substitute(subs2, body));
    }

    /* Idem for a mutually recursive attribute set. */
    ATermList bindings;
    if (atMatch(m, e) >> "Rec" >> bindings) {
        ATermMap subs2(subs);
        for (ATermIterator i(bindings); i; ++i) {
            Expr e;
            if (!(atMatch(m, *i) >> "Bind" >> s >> e))
                abort(); /* can't happen */
            subs2.remove(s);
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
        ATermList out = ATempty;
        for (ATermIterator i((ATermList) e); i; ++i)
            out = ATinsert(out, substitute(subs, *i));
        return (ATerm) ATreverse(out);
    }

    return e;
}


Expr makeBool(bool b)
{
    return b ? ATmake("Bool(True)") : ATmake("Bool(False)");
}
