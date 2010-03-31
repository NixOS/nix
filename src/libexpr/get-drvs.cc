#include "get-drvs.hh"
#include "nixexpr-ast.hh"
#include "util.hh"


namespace nix {


string DrvInfo::queryDrvPath(EvalState & state) const
{
    if (drvPath == "") {
        Bindings::iterator i = attrs->find(toATerm("drvPath"));
        PathSet context;
        (string &) drvPath = i != attrs->end() ? state.coerceToPath(i->second, context) : "";
    }
    return drvPath;
}


#if 0
string DrvInfo::queryOutPath(EvalState & state) const
{
    if (outPath == "") {
        Expr a = attrs->get(toATerm("outPath"));
        if (!a) throw TypeError("output path missing");
        PathSet context;
        (string &) outPath = coerceToPath(state, a, context);
    }
    return outPath;
}


MetaInfo DrvInfo::queryMetaInfo(EvalState & state) const
{
    MetaInfo meta;
    
    Expr a = attrs->get(toATerm("meta"));
    if (!a) return meta; /* fine, empty meta information */

    ATermMap attrs2;
    queryAllAttrs(evalExpr(state, a), attrs2);

    for (ATermMap::const_iterator i = attrs2.begin(); i != attrs2.end(); ++i) {
        Expr e = evalExpr(state, i->value);
        string s;
        PathSet context;
        MetaValue value;
        int n;
        ATermList es;
        if (matchStr(e, s, context)) {
            value.type = MetaValue::tpString;
            value.stringValue = s;
            meta[aterm2String(i->key)] = value;
        } else if (matchInt(e, n)) {
            value.type = MetaValue::tpInt;
            value.intValue = n;
            meta[aterm2String(i->key)] = value;
        } else if (matchList(e, es)) {
            value.type = MetaValue::tpStrings;
            for (ATermIterator j(es); j; ++j)
                value.stringValues.push_back(evalStringNoCtx(state, *j));
            meta[aterm2String(i->key)] = value;
        }
    }

    return meta;
}


MetaValue DrvInfo::queryMetaInfo(EvalState & state, const string & name) const
{
    /* !!! evaluates all meta attributes => inefficient */
    return queryMetaInfo(state)[name];
}


void DrvInfo::setMetaInfo(const MetaInfo & meta)
{
    ATermMap metaAttrs;
    foreach (MetaInfo::const_iterator, i, meta) {
        Expr e;
        switch (i->second.type) {
            case MetaValue::tpInt: e = makeInt(i->second.intValue); break;
            case MetaValue::tpString: e = makeStr(i->second.stringValue); break;
            case MetaValue::tpStrings: {
                ATermList es = ATempty;
                foreach (Strings::const_iterator, j, i->second.stringValues)
                    es = ATinsert(es, makeStr(*j));
                e = makeList(ATreverse(es));
                break;
            }
            default: abort();
        }
        metaAttrs.set(toATerm(i->first), makeAttrRHS(e, makeNoPos()));
    }
    attrs->set(toATerm("meta"), makeAttrs(metaAttrs));
}
#endif


/* Cache for already considered values. */
typedef set<Value *> Values;


/* Evaluate value `v'.  If it evaluates to an attribute set of type
   `derivation', then put information about it in `drvs' (unless it's
   already in `doneExprs').  The result boolean indicates whether it
   makes sense for the caller to recursively search for derivations in
   `v'. */
static bool getDerivation(EvalState & state, Value & v,
    const string & attrPath, DrvInfos & drvs, Values & doneValues)
{
    try {
        state.forceValue(v);
        if (v.type != tAttrs) return true;

        Bindings::iterator i = v.attrs->find(toATerm("type"));
        if (i == v.attrs->end() || state.forceStringNoCtx(i->second) != "derivation") return true;

        /* Remove spurious duplicates (e.g., an attribute set like
           `rec { x = derivation {...}; y = x;}'. */
        if (doneValues.find(&v) != doneValues.end()) return false;
        doneValues.insert(&v);

        DrvInfo drv;
    
        i = v.attrs->find(toATerm("name"));
        /* !!! We really would like to have a decent back trace here. */
        if (i == v.attrs->end()) throw TypeError("derivation name missing");
        drv.name = state.forceStringNoCtx(i->second);

        i = v.attrs->find(toATerm("system"));
        if (i == v.attrs->end())
            drv.system = "unknown";
        else
            drv.system = state.forceStringNoCtx(i->second);

        drv.attrs = v.attrs;

        drv.attrPath = attrPath;

        drvs.push_back(drv);
        return false;
    
    } catch (AssertionError & e) {
        return false;
    }
}


bool getDerivation(EvalState & state, Value & v, DrvInfo & drv)
{
    Values doneValues;
    DrvInfos drvs;
    getDerivation(state, v, "", drvs, doneValues);
    if (drvs.size() != 1) return false;
    drv = drvs.front();
    return true;
}


static string addToPath(const string & s1, const string & s2)
{
    return s1.empty() ? s2 : s1 + "." + s2;
}


static void getDerivations(EvalState & state, Value & v,
    const string & pathPrefix, const ATermMap & autoArgs,
    DrvInfos & drvs, Values & doneValues)
{
    // !!! autoCallFunction(evalExpr(state, e), autoArgs)
    
    /* Process the expression. */
    DrvInfo drv;

    if (!getDerivation(state, v, pathPrefix, drvs, doneValues)) ;

    else if (v.type == tAttrs) {

        /* !!! undocumented hackery to support combining channels in
           nix-env.cc. */
        bool combineChannels = v.attrs->find(toATerm("_combineChannels")) != v.attrs->end();

        /* Consider the attributes in sorted order to get more
           deterministic behaviour in nix-env operations (e.g. when
           there are names clashes between derivations, the derivation
           bound to the attribute with the "lower" name should take
           precedence). */
        StringSet attrs;
        foreach (Bindings::iterator, i, *v.attrs)
            attrs.insert(aterm2String(i->first));

        foreach (StringSet::iterator, i, attrs) {
            startNest(nest, lvlDebug, format("evaluating attribute `%1%'") % *i);
            string pathPrefix2 = addToPath(pathPrefix, *i);
            Value & v2((*v.attrs)[toATerm(*i)]);
            if (combineChannels)
                getDerivations(state, v2, pathPrefix2, autoArgs, drvs, doneValues);
            else if (getDerivation(state, v2, pathPrefix2, drvs, doneValues)) {
                /* If the value of this attribute is itself an
                   attribute set, should we recurse into it?  => Only
                   if it has a `recurseForDerivations = true'
                   attribute. */
                if (v2.type == tAttrs) {
                    Bindings::iterator j = v2.attrs->find(toATerm("recurseForDerivations"));
                    if (j != v2.attrs->end() && state.forceBool(j->second))
                        getDerivations(state, v2, pathPrefix2, autoArgs, drvs, doneValues);
                }
            }
        }
    }

    else if (v.type == tList) {
        for (unsigned int n = 0; n < v.list.length; ++n) {
            startNest(nest, lvlDebug,
                format("evaluating list element"));
            string pathPrefix2 = addToPath(pathPrefix, (format("%1%") % n).str());
            if (getDerivation(state, v.list.elems[n], pathPrefix2, drvs, doneValues))
                getDerivations(state, v.list.elems[n], pathPrefix2, autoArgs, drvs, doneValues);
        }
    }

    else throw TypeError("expression does not evaluate to a derivation (or a set or list of those)");
}


void getDerivations(EvalState & state, Value & v, const string & pathPrefix,
    const ATermMap & autoArgs, DrvInfos & drvs)
{
    Values doneValues;
    getDerivations(state, v, pathPrefix, autoArgs, drvs, doneValues);
}

 
}
