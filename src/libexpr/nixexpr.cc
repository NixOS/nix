#include "nixexpr.hh"
#include "derivations.hh"


#include "nixexpr-ast.hh"
#include "nixexpr-ast.cc"


ATermMap::ATermMap(unsigned int initialSize, unsigned int maxLoadPct)
    : table(0)
{
    this->maxLoadPct = maxLoadPct;
    table = ATtableCreate(initialSize, maxLoadPct);
    if (!table) throw Error("cannot create ATerm table");
}


ATermMap::ATermMap(const ATermMap & map)
    : table(0)
{
    copy(map);
}


ATermMap::~ATermMap()
{
    free();
}


ATermMap & ATermMap::operator = (const ATermMap & map)
{
    if (this == &map) return *this;
    free();
    copy(map);
    return *this;
}


void ATermMap::free()
{
    if (table) {
        ATtableDestroy(table);
        table = 0;
    }
}


void ATermMap::copy(const ATermMap & map)
{
    ATermList keys = map.keys();

    /* !!! We adjust for the maximum load pct by allocating twice as
       much.  Probably a bit too much. */
    maxLoadPct = map.maxLoadPct;
    table = ATtableCreate(ATgetLength(keys) * 2, maxLoadPct);
    if (!table) throw Error("cannot create ATerm table");

    add(map, keys);
}


void ATermMap::set(ATerm key, ATerm value)
{
    return ATtablePut(table, key, value);
}


void ATermMap::set(const string & key, ATerm value)
{
    set(toATerm(key), value);
}


ATerm ATermMap::get(ATerm key) const
{
    return ATtableGet(table, key);
}


ATerm ATermMap::get(const string & key) const
{
    return get(toATerm(key));
}


void ATermMap::remove(ATerm key)
{
    ATtableRemove(table, key);
}


void ATermMap::remove(const string & key)
{
    remove(toATerm(key));
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


string showPos(ATerm pos)
{
    ATerm path;
    int line, column;
    if (matchNoPos(pos)) return "undefined position";
    if (!matchPos(pos, path, line, column))
        throw badTerm("position expected", pos);
    return (format("`%1%', line %2%") % aterm2String(path) % line).str();
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


void queryAllAttrs(Expr e, ATermMap & attrs, bool withPos)
{
    ATermList bnds;
    if (!matchAttrs(e, bnds))
        throw Error("attribute set expected");

    for (ATermIterator i(bnds); i; ++i) {
        ATerm name;
        Expr e;
        ATerm pos;
        if (!matchBind(*i, name, e, pos)) abort(); /* can't happen */
        attrs.set(name, withPos ? makeAttrRHS(e, pos) : e);
    }
}


Expr queryAttr(Expr e, const string & name)
{
    ATerm dummy;
    return queryAttr(e, name, dummy);
}


Expr queryAttr(Expr e, const string & name, ATerm & pos)
{
    ATermList bnds;
    if (!matchAttrs(e, bnds))
        throw Error("attribute set expected");

    for (ATermIterator i(bnds); i; ++i) {
        ATerm name2, pos2;
        Expr e;
        if (!matchBind(*i, name2, e, pos2))
            abort(); /* can't happen */
        if (aterm2String(name2) == name) {
            pos = pos2;
            return e;
        }
    }

    return 0;
}


Expr makeAttrs(const ATermMap & attrs)
{
    ATermList bnds = ATempty;
    for (ATermIterator i(attrs.keys()); i; ++i) {
        Expr e;
        ATerm pos;
        if (!matchAttrRHS(attrs.get(*i), e, pos))
            abort(); /* can't happen */
        bnds = ATinsert(bnds, makeBind(*i, e, pos));
    }
    return makeAttrs(ATreverse(bnds));
}


Expr substitute(const ATermMap & subs, Expr e)
{
    checkInterrupt();

    //if (subs.size() == 0) return e;

    ATerm name, pos, e2;

    /* As an optimisation, don't substitute in subterms known to be
       closed. */
    if (matchClosed(e, e2)) return e;

    if (matchVar(e, name)) {
        Expr sub = subs.get(name);
        Expr wrapped;
        /* Add a "closed" wrapper around terms that aren't already
           closed.  The check is necessary to prevent repeated
           wrapping, e.g., closed(closed(closed(...))), which kills
           caching. */
        return sub ? (matchClosed(sub, wrapped) ? sub : makeClosed(sub)) : e;
    }

    /* In case of a function, filter out all variables bound by this
       function. */
    ATermList formals;
    ATerm body, def;
    if (matchFunction(e, formals, body, pos)) {
        ATermMap subs2(subs);
        for (ATermIterator i(formals); i; ++i) {
            if (!matchNoDefFormal(*i, name) &&
                !matchDefFormal(*i, name, def))
                abort();
            subs2.remove(name);
        }
        return makeFunction(
            (ATermList) substitute(subs2, (ATerm) formals),
            substitute(subs2, body), pos);
    }

    if (matchFunction1(e, name, body, pos)) {
        ATermMap subs2(subs);
        subs2.remove(name);
        return makeFunction1(name, substitute(subs2, body), pos);
    }
        
    /* Idem for a mutually recursive attribute set. */
    ATermList rbnds, nrbnds;
    if (matchRec(e, rbnds, nrbnds)) {
        ATermMap subs2(subs);
        for (ATermIterator i(rbnds); i; ++i)
            if (matchBind(*i, name, e2, pos)) subs2.remove(name);
            else abort(); /* can't happen */
        for (ATermIterator i(nrbnds); i; ++i)
            if (matchBind(*i, name, e2, pos)) subs2.remove(name);
            else abort(); /* can't happen */
        return makeRec(
            (ATermList) substitute(subs2, (ATerm) rbnds),
            (ATermList) substitute(subs, (ATerm) nrbnds));
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
    ATermList formals;
    ATerm with, body;
    ATermList rbnds, nrbnds;

    if (matchVar(e, name)) {
        if (!defs.get(name))
            throw Error(format("undefined variable `%1%'")
                % aterm2String(name));
    }

    else if (matchFunction(e, formals, body, pos)) {
        ATermMap defs2(defs);
        for (ATermIterator i(formals); i; ++i) {
            Expr deflt;
            if (!matchNoDefFormal(*i, name) &&
                !matchDefFormal(*i, name, deflt))
                abort();
            defs2.set(name, (ATerm) ATempty);
        }
        for (ATermIterator i(formals); i; ++i) {
            Expr deflt;
            if (matchDefFormal(*i, name, deflt))
                checkVarDefs2(done, defs2, deflt);
        }
        checkVarDefs2(done, defs2, body);
    }
        
    else if (matchFunction1(e, name, body, pos)) {
        ATermMap defs2(defs);
        defs2.set(name, (ATerm) ATempty);
        checkVarDefs2(done, defs2, body);
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
        checkVarDefs2(done, defs2, (ATerm) rbnds);
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


Expr makeBool(bool b)
{
    return b ? eTrue : eFalse;
}
