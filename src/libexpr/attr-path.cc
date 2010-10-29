#include "attr-path.hh"
#include "util.hh"


namespace nix {


// !!! Shouldn't we return a pointer to a Value?
void findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Expr * e, Value & v)
{
    Strings tokens = tokenizeString(attrPath, ".");

    Error attrError =
        Error(format("attribute selection path `%1%' does not match expression") % attrPath);

    string curPath;

    state.mkThunk_(v, e);
    
    foreach (Strings::iterator, i, tokens) {

        if (!curPath.empty()) curPath += ".";
        curPath += *i;

        /* Is *i an index (integer) or a normal attribute name? */
        enum { apAttr, apIndex } apType = apAttr;
        string attr = *i;
        int attrIndex = -1;
        if (string2Int(attr, attrIndex)) apType = apIndex;

        /* Evaluate the expression. */
        Value vTmp;
        state.autoCallFunction(autoArgs, v, vTmp);
        v = vTmp;
        state.forceValue(v);

        /* It should evaluate to either an attribute set or an
           expression, according to what is specified in the
           attrPath. */

        if (apType == apAttr) {

            if (v.type != tAttrs)
                throw TypeError(
                    format("the expression selected by the selection path `%1%' should be an attribute set but is %2%")
                    % curPath % showType(v));

            Bindings::iterator a = v.attrs->find(state.symbols.create(attr));
            if (a == v.attrs->end())
                throw Error(format("attribute `%1%' in selection path `%2%' not found") % attr % curPath);
            v = *a->value;
        }

        else if (apType == apIndex) {

            if (v.type != tList)
                throw TypeError(
                    format("the expression selected by the selection path `%1%' should be a list but is %2%")
                    % curPath % showType(v));

            if (attrIndex >= v.list.length)
                throw Error(format("list index %1% in selection path `%2%' is out of range") % attrIndex % curPath);

            v = *v.list.elems[attrIndex];
        }
        
    }
}

 
}
