#include "nix/expr/attr-path.hh"
#include "nix/expr/eval-inline.hh"

namespace nix {

static Strings parseAttrPath(std::string_view s)
{
    Strings res;
    std::string cur;
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
                if (*i == '"')
                    break;
                cur.push_back(*i++);
            }
        } else
            cur.push_back(*i);
        ++i;
    }
    if (!cur.empty())
        res.push_back(cur);
    return res;
}

std::vector<Symbol> parseAttrPath(EvalState & state, std::string_view s)
{
    std::vector<Symbol> res;
    for (auto & a : parseAttrPath(s))
        res.push_back(state.symbols.create(a));
    return res;
}

std::pair<Value *, PosIdx>
findAlongAttrPath(EvalState & state, const std::string & attrPath, Bindings & autoArgs, Value & vIn)
{
    Strings tokens = parseAttrPath(attrPath);

    Value * v = &vIn;
    PosIdx pos = noPos;

    for (auto & attr : tokens) {

        /* Is i an index (integer) or a normal attribute name? */
        auto attrIndex = string2Int<unsigned int>(attr);

        /* Evaluate the expression. */
        Value * vNew = state.allocValue();
        state.autoCallFunction(autoArgs, *v, *vNew);
        v = vNew;
        state.forceValue(*v, noPos);

        /* It should evaluate to either a set or an expression,
           according to what is specified in the attrPath. */

        if (!attrIndex) {

            if (v->type() != nAttrs)
                state
                    .error<TypeError>(
                        "the expression selected by the selection path '%1%' should be a set but is %2%",
                        attrPath,
                        showType(*v))
                    .debugThrow();
            if (attr.empty())
                throw Error("empty attribute name in selection path '%1%'", attrPath);

            auto a = v->attrs()->get(state.symbols.create(attr));
            if (!a) {
                StringSet attrNames;
                for (auto & attr : *v->attrs())
                    attrNames.insert(std::string(state.symbols[attr.name]));

                auto suggestions = Suggestions::bestMatches(attrNames, attr);
                throw AttrPathNotFound(
                    suggestions, "attribute '%1%' in selection path '%2%' not found", attr, attrPath);
            }
            v = &*a->value;
            pos = a->pos;
        }

        else {

            if (!v->isList())
                state
                    .error<TypeError>(
                        "the expression selected by the selection path '%1%' should be a list but is %2%",
                        attrPath,
                        showType(*v))
                    .debugThrow();
            if (*attrIndex >= v->listSize())
                throw AttrPathNotFound("list index %1% in selection path '%2%' is out of range", *attrIndex, attrPath);

            v = v->listView()[*attrIndex];
            pos = noPos;
        }
    }

    return {v, pos};
}

std::pair<SourcePath, uint32_t> findPackageFilename(EvalState & state, Value & v, std::string what)
{
    Value * v2;
    try {
        auto & dummyArgs = Bindings::emptyBindings;
        v2 = findAlongAttrPath(state, "meta.position", dummyArgs, v).first;
    } catch (Error &) {
        throw NoPositionInfo("package '%s' has no source location information", what);
    }

    // FIXME: is it possible to extract the Pos object instead of doing this
    //        toString + parsing?
    NixStringContext context;
    auto path =
        state.coerceToPath(noPos, *v2, context, "while evaluating the 'meta.position' attribute of a derivation");

    auto fn = path.path.abs();

    auto fail = [fn]() { throw ParseError("cannot parse 'meta.position' attribute '%s'", fn); };

    try {
        auto colon = fn.rfind(':');
        if (colon == std::string::npos)
            fail();
        auto lineno = std::stoi(std::string(fn, colon + 1, std::string::npos));
        return {SourcePath{path.accessor, CanonPath(fn.substr(0, colon))}, lineno};
    } catch (std::invalid_argument & e) {
        fail();
        unreachable();
    }
}

} // namespace nix
