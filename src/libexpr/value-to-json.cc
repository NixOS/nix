#include "value-to-json.hh"
#include "eval-inline.hh"
#include "util.hh"

#include <cstdlib>
#include <iomanip>
#include <nlohmann/json.hpp>


namespace nix {

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, nlohmann::json & out, PathSet & context)
{
    checkInterrupt();

    if (strict) state.forceValue(v);

    switch (v.type) {

        case tInt:
            out = v.integer;
            break;

        case tBool:
            out = v.boolean;
            break;

        case tString:
            copyContext(v, context);
            out = v.string.s;
            break;

        case tPath:
            out = state.copyPathToStore(context, v.path);
            break;

        case tNull:
            out = nullptr;
            break;

        case tAttrs: {
            auto maybeString = state.tryAttrsToString(noPos, v, context, false, false);
            if (maybeString) {
                out = *maybeString;
                break;
            }
            auto i = v.attrs->find(state.sOutPath);
            if (i == v.attrs->end()) {
                out = nlohmann::json::object();
                StringSet names;
                for (auto & j : *v.attrs)
                    names.insert(j.name);
                for (auto & j : names) {
                    Attr & a(*v.attrs->find(state.symbols.create(j)));
                    auto & placeholder = out[j];
                    printValueAsJSON(state, strict, *a.value, placeholder, context);
                }
            } else
                printValueAsJSON(state, strict, *i->value, out, context);
            break;
        }

        case tList1: case tList2: case tListN: {
            for (unsigned int n = 0; n < v.listSize(); ++n) {
                auto placeholder = nlohmann::json::array();
                printValueAsJSON(state, strict, *v.listElems()[n], placeholder, context);
                out.push_back(placeholder);
            }
            break;
        }

        case tExternal:
            v.external->printValueAsJSON(state, strict, out, context);
            break;

        case tFloat:
            out = v.fpoint;
            break;

        default:
            throw TypeError("cannot convert %1% to JSON", showType(v));
    }
}

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & str, PathSet & context)
{
    nlohmann::json out;
    printValueAsJSON(state, strict, v, out, context);
    str << out;
}

void ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
    nlohmann::json & out, PathSet & context) const
{
    throw TypeError("cannot convert %1% to JSON", showType());
}


}
