#include "get-drvs.hh"
#include "nixexpr-ast.hh"


bool getDerivation(EvalState & state, Expr e, DrvInfo & drv)
{
    ATermList es;
    e = evalExpr(state, e);
    if (!matchAttrs(e, es)) return false;

    ATermMap attrs;
    queryAllAttrs(e, attrs, false);
    
    Expr a = attrs.get("type");
    if (!a || evalString(state, a) != "derivation") return false;

    a = attrs.get("name");
    if (!a) throw badTerm("derivation name missing", e);
    drv.name = evalString(state, a);

    a = attrs.get("system");
    if (!a)
        drv.system = "unknown";
    else
        drv.system = evalString(state, a);

    drv.attrs = attrs;

    return true;
}


void getDerivations(EvalState & state, Expr e, DrvInfos & drvs)
{
    ATermList es;
    DrvInfo drv;

    e = evalExpr(state, e);

    if (getDerivation(state, e, drv)) 
        drvs.push_back(drv);

    else if (matchAttrs(e, es)) {
        ATermMap drvMap;
        queryAllAttrs(e, drvMap);
        for (ATermIterator i(drvMap.keys()); i; ++i) {
            debug(format("evaluating attribute `%1%'") % aterm2String(*i));
            if (getDerivation(state, drvMap.get(*i), drv))
                drvs.push_back(drv);
            else
                ;
                //                parseDerivations(state, drvMap.get(*i), drvs);
        }
    }

    else if (matchList(e, es)) {
        for (ATermIterator i(es); i; ++i) {
            debug(format("evaluating list element"));
            if (getDerivation(state, *i, drv))
                drvs.push_back(drv);
            else
                getDerivations(state, *i, drvs);
        }
    }
}


