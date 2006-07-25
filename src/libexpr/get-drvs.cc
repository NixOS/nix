#include "get-drvs.hh"
#include "nixexpr-ast.hh"


string DrvInfo::queryDrvPath(EvalState & state) const
{
    if (drvPath == "") {
        Expr a = attrs->get(toATerm("drvPath"));
        (string &) drvPath = a ? evalPath(state, a) : "";
    }
    return drvPath;
}


string DrvInfo::queryOutPath(EvalState & state) const
{
    if (outPath == "") {
        Expr a = attrs->get(toATerm("outPath"));
        if (!a) throw TypeError("output path missing");
        (string &) outPath = evalPath(state, a);
    }
    return outPath;
}


MetaInfo DrvInfo::queryMetaInfo(EvalState & state) const
{
    MetaInfo meta;
    
    Expr a = attrs->get(toATerm("meta"));
    if (!a) return meta; /* fine, empty meta information */

    ATermMap attrs2(16); /* !!! */
    queryAllAttrs(evalExpr(state, a), attrs2);

    for (ATermMap::const_iterator i = attrs2.begin(); i != attrs2.end(); ++i) {
        ATerm s = coerceToString(evalExpr(state, i->value));
        if (s)
            meta[aterm2String(i->key)] = aterm2String(s);
        /* For future compatibility, ignore attribute values that are
           not strings. */
    }

    return meta;
}


/* Cache for already evaluated derivations.  Usually putting ATerms in
   a STL container is unsafe (they're not scanning for GC roots), but
   here it doesn't matter; everything in this set is reachable from
   the stack as well. */
typedef set<Expr> Exprs;


/* Evaluate expression `e'.  If it evaluates to an attribute set of
   type `derivation', then put information about it in `drvs' (unless
   it's already in `doneExprs').  The result boolean indicates whether
   it makes sense for the caller to recursively search for derivations
   in `e'. */
static bool getDerivation(EvalState & state, Expr e,
    DrvInfos & drvs, Exprs & doneExprs, string attributeName)
{
    try {
        
        ATermList es;
        e = evalExpr(state, e);
        if (!matchAttrs(e, es)) return true;

        shared_ptr<ATermMap> attrs(new ATermMap(32)); /* !!! */
        queryAllAttrs(e, *attrs, false);
    
        Expr a = attrs->get(toATerm("type"));
        if (!a || evalString(state, a) != "derivation") return true;

        /* Remove spurious duplicates (e.g., an attribute set like
           `rec { x = derivation {...}; y = x;}'. */
        if (doneExprs.find(e) != doneExprs.end()) return false;
        doneExprs.insert(e);

        DrvInfo drv;
    
        a = attrs->get(toATerm("name"));
        /* !!! We really would like to have a decent back trace here. */
        if (!a) throw TypeError("derivation name missing");
        drv.name = evalString(state, a);

        a = attrs->get(toATerm("system"));
        if (!a)
            drv.system = "unknown";
        else
            drv.system = evalString(state, a);

        drv.attrs = attrs;

        drv.attrPath = attributeName;

        drvs.push_back(drv);
        return false;
    
    } catch (AssertionError & e) {
        return false;
    }
}


bool getDerivation(EvalState & state, Expr e, DrvInfo & drv)
{
    Exprs doneExprs;
    DrvInfos drvs;
    getDerivation(state, e, drvs, doneExprs, "");
    if (drvs.size() != 1) return false;
    drv = drvs.front();
    return true;
}


static string addToPath(const string & s1, const string & s2)
{
    return s1.empty() ? s2 : s1 + "." + s2;
}


static void getDerivations(EvalState & state, Expr e,
    DrvInfos & drvs, Exprs & doneExprs, const string & attrPath,
    const string & pathTaken)
{
    /* Automatically call functions that have defaults for all
       arguments. */
    ATermList formals;
    ATerm body, pos;
    if (matchFunction(e, formals, body, pos)) {
        for (ATermIterator i(formals); i; ++i) {
            Expr name, def; ATerm values, def2;
            if (!matchFormal(*i, name, values, def2)) abort();
            if (!matchDefaultValue(def2, def)) 
                throw TypeError(format("cannot auto-call a function that has an argument without a default value (`%1%')")
                    % aterm2String(name));
        }
        getDerivations(state,
            makeCall(e, makeAttrs(ATermMap(0))),
            drvs, doneExprs, attrPath, pathTaken);
        return;
    }

    /* Parse the start of attrPath. */
    enum { apNone, apAttr, apIndex } apType;
    string attrPathRest;
    string attr;
    int attrIndex;
    Error attrError =
        Error(format("attribute selection path `%1%' does not match expression") % attrPath);

    if (attrPath.empty())
        apType = apNone;
    else {
        string::size_type dot = attrPath.find(".");
        if (dot == string::npos) {
            attrPathRest = "";
            attr = attrPath;
        } else {
            attrPathRest = string(attrPath, dot + 1);
            attr = string(attrPath, 0, dot);
        }
        apType = apAttr;
        if (string2Int(attr, attrIndex)) apType = apIndex;
    }

    /* Process the expression. */
    ATermList es;
    DrvInfo drv;

    if (!getDerivation(state, e, drvs, doneExprs, pathTaken)) {
        if (apType != apNone) throw attrError;
        return;
    }

    e = evalExpr(state, e);

    if (matchAttrs(e, es)) {
        if (apType != apNone && apType != apAttr) throw attrError;
        ATermMap drvMap(ATgetLength(es));
        queryAllAttrs(e, drvMap);
        if (apType == apNone) {
            for (ATermMap::const_iterator i = drvMap.begin(); i != drvMap.end(); ++i) {
                startNest(nest, lvlDebug,
                    format("evaluating attribute `%1%'") % aterm2String(i->key));
                string pathTaken2 = addToPath(pathTaken, aterm2String(i->key));
                if (getDerivation(state, i->value, drvs, doneExprs, pathTaken2)) {
                    /* If the value of this attribute is itself an
                       attribute set, should we recurse into it?
                       => Only if it has a `recurseForDerivations = true'
                       attribute. */
                    ATermList es;
                    Expr e = evalExpr(state, i->value);
                    if (matchAttrs(e, es)) {
                        ATermMap attrs(ATgetLength(es));
                        queryAllAttrs(e, attrs, false);
                        Expr e2 = attrs.get(toATerm("recurseForDerivations"));
                        if (e2 && evalBool(state, e2))
                            getDerivations(state, e, drvs, doneExprs, attrPathRest, pathTaken2);
                    }
                }
            }
        } else {
            Expr e2 = drvMap.get(toATerm(attr));
            if (!e2) throw Error(format("attribute `%1%' in selection path not found") % attr);
            startNest(nest, lvlDebug,
                format("evaluating attribute `%1%'") % attr);
            string pathTaken2 = addToPath(pathTaken, attr);
            getDerivation(state, e2, drvs, doneExprs, pathTaken2);
            if (!attrPath.empty())
                getDerivations(state, e2, drvs, doneExprs, attrPathRest, pathTaken2);
        }
        return;
    }

    if (matchList(e, es)) {
        if (apType != apNone && apType != apIndex) throw attrError;
        if (apType == apNone) {
            int n = 0;
            for (ATermIterator i(es); i; ++i, ++n) {
                startNest(nest, lvlDebug,
                    format("evaluating list element"));
                string pathTaken2 = addToPath(pathTaken, (format("%1%") % n).str());
                if (getDerivation(state, *i, drvs, doneExprs, pathTaken2))
                    getDerivations(state, *i, drvs, doneExprs, attrPathRest, pathTaken2);
            }
        } else {
            Expr e2 = ATelementAt(es, attrIndex);
            if (!e2) throw Error(format("list index %1% in selection path not found") % attrIndex);
            startNest(nest, lvlDebug,
                format("evaluating list element"));
            string pathTaken2 = addToPath(pathTaken, (format("%1%") % attrIndex).str());
            if (getDerivation(state, e2, drvs, doneExprs, pathTaken2))
                getDerivations(state, e2, drvs, doneExprs, attrPathRest, pathTaken2);
        }
        return;
    }

    throw TypeError("expression does not evaluate to a derivation (or a set or list of those)");
}


void getDerivations(EvalState & state, Expr e, DrvInfos & drvs,
    const string & attrPath)
{
    Exprs doneExprs;
    getDerivations(state, e, drvs, doneExprs, attrPath, "");
}
