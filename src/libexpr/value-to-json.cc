#include "value-to-json.hh"
#include "eval-inline.hh"
#include "util.hh"

#include <cstdlib>
#include <iomanip>
#include <nlohmann/json.hpp>


namespace nix {

nlohmann::json printValueAsJSON(EvalState & state, bool strict,
    Value & v, PathSet & context)
{
    checkInterrupt();

    if (strict) state.forceValue(v);

    switch (v.type) {

        case tInt:
            return v.integer;

        case tBool:
            return v.boolean;

        case tString:
            copyContext(v, context);
            return v.string.s;

        case tPath:
            return state.copyPathToStore(context, v.path);

        case tNull:
            return nullptr;

        case tAttrs: {
            auto maybeString = state.tryAttrsToString(noPos, v, context, false, false);
            if (maybeString) {
                return *maybeString;
            }
            auto i = v.attrs->find(state.sOutPath);
            if (i == v.attrs->end()) {
                auto out = nlohmann::json::object();
                StringSet names;
                for (auto & j : *v.attrs)
                    names.insert(j.name);
                for (auto & j : names) {
                    Attr & a(*v.attrs->find(state.symbols.create(j)));
                    out[j] = printValueAsJSON(state, strict, *a.value, context);
                }
                return out;
            } else
                return printValueAsJSON(state, strict, *i->value, context);
            break;
        }

        case tList1: case tList2: case tListN: {
            auto out = nlohmann::json::array();
            for (unsigned int n = 0; n < v.listSize(); ++n) {
                out.push_back(printValueAsJSON(state, strict, *v.listElems()[n], context));
            }
            return out;
        }

        case tExternal:
            return v.external->printValueAsJSON(state, strict, context);

        case tFloat:
            return v.fpoint;

        default:
            throw TypeError("cannot convert %1% to JSON", showType(v));
    }
}

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & str, PathSet & context)
{
    str << printValueAsJSON(state, strict, v, context);
}

nlohmann::json ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
    PathSet & context) const
{
    throw TypeError("cannot convert %1% to JSON", showType());
}


}
