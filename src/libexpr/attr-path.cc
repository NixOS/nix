#include "attr-path.hh"
#include "eval-inline.hh"
#include "util.hh"


namespace nix {


static Strings parseAttrPath(const string & s)
{
    Strings res;
    string cur;
    string::const_iterator i = s.begin();
    while (i != s.end()) {
        if (*i == '.') {
            res.push_back(cur);
            cur.clear();
        } else if (*i == '"') {
            ++i;
            while (1) {
                if (i == s.end())
                    throw Error(format("missing closing quote in selection path '%1%'") % s);
                if (*i == '"') break;
                cur.push_back(*i++);
            }
        } else
            cur.push_back(*i);
        ++i;
    }
    if (!cur.empty()) res.push_back(cur);
    return res;
}


Value * findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Value & vIn)
{
    Strings tokens = parseAttrPath(attrPath);

    Error attrError =
        Error(format("attribute selection path '%1%' does not match expression") % attrPath);

    Value * v = &vIn;

    for (auto & attr : tokens) {

        /* Is i an index (integer) or a normal attribute name? */
        enum { apAttr, apIndex } apType = apAttr;
        unsigned int attrIndex;
        if (string2Int(attr, attrIndex)) apType = apIndex;

        /* Evaluate the expression. */
        Value * vNew = state.allocValue();
        state.autoCallFunction(autoArgs, *v, *vNew);
        v = vNew;
        state.forceValue(*v);

        /* It should evaluate to either a set or an expression,
           according to what is specified in the attrPath. */

        if (apType == apAttr) {

            if (v->type != tAttrs)
                throw TypeError(
                    format("the expression selected by the selection path '%1%' should be a set but is %2%")
                    % attrPath % showType(*v));

            if (attr.empty())
                throw Error(format("empty attribute name in selection path '%1%'") % attrPath);

            Bindings::iterator a = v->attrs->find(state.symbols.create(attr));
            if (a == v->attrs->end())
                throw Error(format("attribute '%1%' in selection path '%2%' not found") % attr % attrPath);
            v = &*a->value;
        }

        else if (apType == apIndex) {

            if (!v->isList())
                throw TypeError(
                    format("the expression selected by the selection path '%1%' should be a list but is %2%")
                    % attrPath % showType(*v));

            if (attrIndex >= v->listSize())
                throw Error(format("list index %1% in selection path '%2%' is out of range") % attrIndex % attrPath);

            v = v->listElems()[attrIndex];
        }

    }

    return v;
}


std::tuple<std::string, int> findDerivationFilename(EvalState & state, Value & v, std::string what)
{
    Value * v2;
    try {
        auto dummyArgs = state.allocBindings(0);
        v2 = findAlongAttrPath(state, "meta.position", *dummyArgs, v);
    } catch (Error &) {
        throw Error("package '%s' has no source location information", what);
    }

    auto pos = state.forceString(*v2);

    auto colon = pos.rfind(':');
    if (colon == std::string::npos)
        throw Error("cannot parse meta.position attribute '%s'", pos);

    std::string filename(pos, 0, colon);
    int lineno;
    try {
        lineno = std::stoi(std::string(pos, colon + 1));
    } catch (std::invalid_argument & e) {
        throw Error("cannot parse line number '%s'", pos);
    }

    return std::make_tuple(filename, lineno);
}


}
