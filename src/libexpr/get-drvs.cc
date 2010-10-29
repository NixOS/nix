#include "get-drvs.hh"
#include "util.hh"


namespace nix {


string DrvInfo::queryDrvPath(EvalState & state) const
{
    if (drvPath == "" && attrs) {
        Bindings::iterator i = attrs->find(state.sDrvPath);
        PathSet context;
        (string &) drvPath = i != attrs->end() ? state.coerceToPath(*i->value, context) : "";
    }
    return drvPath;
}


string DrvInfo::queryOutPath(EvalState & state) const
{
    if (outPath == "" && attrs) {
        Bindings::iterator i = attrs->find(state.sOutPath);
        PathSet context;
        (string &) outPath = i != attrs->end() ? state.coerceToPath(*i->value, context) : "";
    }
    return outPath;
}


MetaInfo DrvInfo::queryMetaInfo(EvalState & state) const
{
    if (metaInfoRead) return meta;
    
    (bool &) metaInfoRead = true;
    
    Bindings::iterator a = attrs->find(state.sMeta);
    if (a == attrs->end()) return meta; /* fine, empty meta information */

    state.forceAttrs(*a->value);

    foreach (Bindings::iterator, i, *a->value->attrs) {
        MetaValue value;
        state.forceValue(*i->value);
        if (i->value->type == tString) {
            value.type = MetaValue::tpString;
            value.stringValue = i->value->string.s;
        } else if (i->value->type == tInt) {
            value.type = MetaValue::tpInt;
            value.intValue = i->value->integer;
        } else if (i->value->type == tList) {
            value.type = MetaValue::tpStrings;
            for (unsigned int j = 0; j < i->value->list.length; ++j)
                value.stringValues.push_back(state.forceStringNoCtx(*i->value->list.elems[j]));
        } else continue;
        ((MetaInfo &) meta)[i->name] = value;
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
    metaInfoRead = true;
    this->meta = meta;
}


/* Cache for already considered attrsets. */
typedef set<Bindings *> Done;


/* Evaluate value `v'.  If it evaluates to an attribute set of type
   `derivation', then put information about it in `drvs' (unless it's
   already in `doneExprs').  The result boolean indicates whether it
   makes sense for the caller to recursively search for derivations in
   `v'. */
static bool getDerivation(EvalState & state, Value & v,
    const string & attrPath, DrvInfos & drvs, Done & done)
{
    try {
        state.forceValue(v);
        if (!state.isDerivation(v)) return true;

        /* Remove spurious duplicates (e.g., an attribute set like
           `rec { x = derivation {...}; y = x;}'. */
        if (done.find(v.attrs) != done.end()) return false;
        done.insert(v.attrs);

        DrvInfo drv;
    
        Bindings::iterator i = v.attrs->find(state.sName);
        /* !!! We really would like to have a decent back trace here. */
        if (i == v.attrs->end()) throw TypeError("derivation name missing");
        drv.name = state.forceStringNoCtx(*i->value);

        Bindings::iterator i2 = v.attrs->find(state.sSystem);
        if (i2 == v.attrs->end())
            drv.system = "unknown";
        else
            drv.system = state.forceStringNoCtx(*i2->value);

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
    Done done;
    DrvInfos drvs;
    getDerivation(state, v, "", drvs, done);
    if (drvs.size() != 1) return false;
    drv = drvs.front();
    return true;
}


static string addToPath(const string & s1, const string & s2)
{
    return s1.empty() ? s2 : s1 + "." + s2;
}


static void getDerivations(EvalState & state, Value & vIn,
    const string & pathPrefix, Bindings & autoArgs,
    DrvInfos & drvs, Done & done)
{
    Value v;
    state.autoCallFunction(autoArgs, vIn, v);
    
    /* Process the expression. */
    DrvInfo drv;

    if (!getDerivation(state, v, pathPrefix, drvs, done)) ;

    else if (v.type == tAttrs) {

        /* !!! undocumented hackery to support combining channels in
           nix-env.cc. */
        bool combineChannels = v.attrs->find(state.symbols.create("_combineChannels")) != v.attrs->end();

        /* Consider the attributes in sorted order to get more
           deterministic behaviour in nix-env operations (e.g. when
           there are names clashes between derivations, the derivation
           bound to the attribute with the "lower" name should take
           precedence). */
        typedef std::map<string, Symbol> SortedSymbols;
        SortedSymbols attrs;
        foreach (Bindings::iterator, i, *v.attrs)
            attrs.insert(std::pair<string, Symbol>(i->name, i->name));

        foreach (SortedSymbols::iterator, i, attrs) {
            startNest(nest, lvlDebug, format("evaluating attribute `%1%'") % i->first);
            string pathPrefix2 = addToPath(pathPrefix, i->first);
            Value & v2(*v.attrs->find(i->second)->value);
            if (combineChannels)
                getDerivations(state, v2, pathPrefix2, autoArgs, drvs, done);
            else if (getDerivation(state, v2, pathPrefix2, drvs, done)) {
                /* If the value of this attribute is itself an
                   attribute set, should we recurse into it?  => Only
                   if it has a `recurseForDerivations = true'
                   attribute. */
                if (v2.type == tAttrs) {
                    Bindings::iterator j = v2.attrs->find(state.symbols.create("recurseForDerivations"));
                    if (j != v2.attrs->end() && state.forceBool(*j->value))
                        getDerivations(state, v2, pathPrefix2, autoArgs, drvs, done);
                }
            }
        }
    }

    else if (v.type == tList) {
        for (unsigned int n = 0; n < v.list.length; ++n) {
            startNest(nest, lvlDebug,
                format("evaluating list element"));
            string pathPrefix2 = addToPath(pathPrefix, (format("%1%") % n).str());
            if (getDerivation(state, *v.list.elems[n], pathPrefix2, drvs, done))
                getDerivations(state, *v.list.elems[n], pathPrefix2, autoArgs, drvs, done);
        }
    }

    else throw TypeError("expression does not evaluate to a derivation (or a set or list of those)");
}


void getDerivations(EvalState & state, Value & v, const string & pathPrefix,
    Bindings & autoArgs, DrvInfos & drvs)
{
    Done done;
    getDerivations(state, v, pathPrefix, autoArgs, drvs, done);
}

 
}
