#include "attr-path.hh"
#include "nixexpr-ast.hh"


bool isAttrs(EvalState & state, Expr e, ATermMap & attrs)
{
    e = evalExpr(state, e);
    ATermList dummy;
    if (!matchAttrs(e, dummy)) return false;
    queryAllAttrs(e, attrs, false);
    return true;
}


Expr findAlongAttrPath(EvalState & state, const string & attrPath, Expr e)
{
    Strings tokens = tokenizeString(attrPath, ".");

    Error attrError =
        Error(format("attribute selection path `%1%' does not match expression") % attrPath);

    string curPath;
    
    for (Strings::iterator i = tokens.begin(); i != tokens.end(); ++i) {

        if (!curPath.empty()) curPath += ".";
        curPath += *i;

        /* Is *i an index (integer) or a normal attribute name? */
        enum { apAttr, apIndex } apType = apAttr;
        string attr = *i;
        int attrIndex = -1;
        if (string2Int(attr, attrIndex)) apType = apIndex;

        /* Evaluate the expression. */
        e = evalExpr(state, autoCallFunction(evalExpr(state, e), ATermMap(1)));

        /* It should evaluate to either an attribute set or an
           expression, according to what is specified in the
           attrPath. */

        if (apType == apAttr) {

            ATermMap attrs(128);

            if (!isAttrs(state, e, attrs))
                throw TypeError(
                    format("the expression selected by the selection path `%1%' should be an attribute set but is %2%")
                    % curPath % showType(e));
                
            e = attrs.get(toATerm(attr));
            if (!e)
                throw Error(format("attribute `%1%' in selection path `%2%' not found") % attr % curPath);

        }

        else if (apType == apIndex) {

            ATermList es;
            if (!matchList(e, es))
                throw TypeError(
                    format("the expression selected by the selection path `%1%' should be a list but is %2%")
                    % curPath % showType(e));

            e = ATelementAt(es, attrIndex);
            if (!e)
                throw Error(format("list index %1% in selection path `%2%' not found") % attrIndex % curPath);
            
        }
        
    }
    
    return e;
}
