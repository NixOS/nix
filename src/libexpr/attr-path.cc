#include "attr-path.hh"
#include "eval-inline.hh"
#include "util.hh"


namespace nix {


static Strings parseAttrPath(std::string_view s)
{
    Strings res;
    string cur;
    auto i = s.begin();
    while (i != s.end()) {
        if (*i == '.') {
            res.push_back(cur);
            cur.clear();
        } else if (*i == '"') {
            ++i;
            while (1) {
                if (i == s.end())
                    throw ParseError("missing closing quote in selection path '%1%'", s);
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


std::vector<Symbol> parseAttrPath(EvalState & state, std::string_view s)
{
    std::vector<Symbol> res;
    for (auto & a : parseAttrPath(s))
        res.push_back(state.symbols.create(a));
    return res;
}


std::pair<Value *, Pos> findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Value & vIn)
{
    Strings tokens = parseAttrPath(attrPath);

    Value * v = &vIn;
    Pos pos = noPos;

    for (auto & attr : tokens) {

        /* Is i an index (integer) or a normal attribute name? */
        auto attrIndex = string2Int<unsigned int>(attr);

        /* Evaluate the expression. */
        Value * vNew = state.allocValue();
        state.autoCallFunction(autoArgs, *v, *vNew);
        v = vNew;
        state.forceValue(*v);

        /* It should evaluate to either a set or an expression,
           according to what is specified in the attrPath. */

        if (!attrIndex) {

            if (v->type() != nAttrs)
                throw TypeError(
                    "the expression selected by the selection path '%1%' should be a set but is %2%",
                    attrPath,
                    showType(*v));
            if (attr.empty())
                throw Error("empty attribute name in selection path '%1%'", attrPath);

            Bindings::iterator a = v->attrs->find(state.symbols.create(attr));
            if (a == v->attrs->end())
                throw AttrPathNotFound("attribute '%1%' in selection path '%2%' not found", attr, attrPath);
            v = &*a->value;
            pos = *a->pos;
        }

        else {

            if (!v->isList())
                throw TypeError(
                    "the expression selected by the selection path '%1%' should be a list but is %2%",
                    attrPath,
                    showType(*v));
            if (*attrIndex >= v->listSize())
                throw AttrPathNotFound("list index %1% in selection path '%2%' is out of range", *attrIndex, attrPath);

            v = v->listElems()[*attrIndex];
            pos = noPos;
        }

    }

    return {v, pos};
}


Pos findPackageFilename(EvalState & state, Value & v, std::string what)
{
    Value * v2;
    try {
        auto dummyArgs = state.allocBindings(0);
        v2 = findAlongAttrPath(state, "meta.position", *dummyArgs, v).first;
    } catch (Error &) {
        throw NoPositionInfo("package '%s' has no source location information", what);
    }

    // FIXME: is it possible to extract the Pos object instead of doing this
    //        toString + parsing?
    auto pos = state.forceString(*v2);

    auto colon = pos.rfind(':');
    if (colon == std::string::npos)
        throw ParseError("cannot parse meta.position attribute '%s'", pos);

    std::string filename(pos, 0, colon);
    unsigned int lineno;
    try {
        lineno = std::stoi(std::string(pos, colon + 1));
    } catch (std::invalid_argument & e) {
        throw ParseError("cannot parse line number '%s'", pos);
    }

    Symbol file = state.symbols.create(filename);

    return { foFile, file, lineno, 0 };
}


}
