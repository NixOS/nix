#include "nixexpr.hh"
#include "storeexpr.hh"


ATermMap::ATermMap(unsigned int initialSize, unsigned int maxLoadPct)
{
    this->maxLoadPct = maxLoadPct;
    table = ATtableCreate(initialSize, maxLoadPct);
    if (!table) throw Error("cannot create ATerm table");
}


ATermMap::ATermMap(const ATermMap & map)
    : table(0)
{
    ATermList keys = map.keys();

    /* !!! adjust allocation for load pct */
    maxLoadPct = map.maxLoadPct;
    table = ATtableCreate(ATgetLength(keys), maxLoadPct);
    if (!table) throw Error("cannot create ATerm table");

    add(map, keys);
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


void ATermMap::add(const ATermMap & map)
{
    ATermList keys = map.keys();
    add(map, keys);
}


void ATermMap::add(const ATermMap & map, ATermList & keys)
{
    for (ATermIterator i(keys); i; ++i)
        set(*i, map.get(*i));
}


void ATermMap::reset()
{
    ATtableReset(table);
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
    ATerm name;

    if (atMatch(m, e) >> "Var" >> name) {
        Expr sub = subs.get(name);
        return sub ? sub : e;
    }

    /* In case of a function, filter out all variables bound by this
       function. */
    ATermList formals;
    ATerm body;
    if (atMatch(m, e) >> "Function" >> formals >> body) {
        ATermMap subs2(subs);
        for (ATermIterator i(formals); i; ++i) {
            if (!(atMatch(m, *i) >> "NoDefFormal" >> name) &&
                !(atMatch(m, *i) >> "DefFormal" >> name))
                abort();
            subs2.remove(name);
        }
        return ATmake("Function(<term>, <term>)", formals,
            substitute(subs2, body));
    }

    /* Idem for a mutually recursive attribute set. */
    ATermList rbnds, nrbnds;
    if (atMatch(m, e) >> "Rec" >> rbnds >> nrbnds) {
        ATermMap subs2(subs);
        for (ATermIterator i(rbnds); i; ++i)
            if (atMatch(m, *i) >> "Bind" >> name)
                subs2.remove(name);
            else abort(); /* can't happen */
        for (ATermIterator i(nrbnds); i; ++i)
            if (atMatch(m, *i) >> "Bind" >> name)
                subs2.remove(name);
            else abort(); /* can't happen */
        return ATmake("Rec(<term>, <term>)",
            substitute(subs2, (ATerm) rbnds),
            substitute(subs, (ATerm) nrbnds));
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


void checkVarDefs(const ATermMap & defs, Expr e)
{
    ATMatcher m;
    ATerm name;
    ATermList formals;
    ATerm body;
    ATermList rbnds, nrbnds;

    if (atMatch(m, e) >> "Var" >> name) {
        if (!defs.get(name))
            throw Error(format("undefined variable `%1%'")
                % aterm2String(name));
        return;
    }

    else if (atMatch(m, e) >> "Function" >> formals >> body) {
        ATermMap defs2(defs);
        for (ATermIterator i(formals); i; ++i) {
            Expr deflt;
            if (!(atMatch(m, *i) >> "NoDefFormal" >> name))
                if (atMatch(m, *i) >> "DefFormal" >> name >> deflt)
                    checkVarDefs(defs, deflt);
                else
                    abort();
            defs2.set(name, (ATerm) ATempty);
        }
        return checkVarDefs(defs2, body);
    }
        
    else if (atMatch(m, e) >> "Rec" >> rbnds >> nrbnds) {
        checkVarDefs(defs, (ATerm) nrbnds);
        ATermMap defs2(defs);
        for (ATermIterator i(rbnds); i; ++i) {
            if (!(atMatch(m, *i) >> "Bind" >> name))
                abort(); /* can't happen */
            defs2.set(name, (ATerm) ATempty);
        }
        for (ATermIterator i(nrbnds); i; ++i) {
            if (!(atMatch(m, *i) >> "Bind" >> name))
                abort(); /* can't happen */
            defs2.set(name, (ATerm) ATempty);
        }
        checkVarDefs(defs2, (ATerm) rbnds);
    }
    
    else if (ATgetType(e) == AT_APPL) {
        int arity = ATgetArity(ATgetAFun(e));
        for (int i = 0; i < arity; ++i)
            checkVarDefs(defs, ATgetArgument(e, i));
    }

    else if (ATgetType(e) == AT_LIST)
        for (ATermIterator i((ATermList) e); i; ++i)
            checkVarDefs(defs, *i);
}


Expr makeBool(bool b)
{
    return b ? ATmake("Bool(True)") : ATmake("Bool(False)");
}
