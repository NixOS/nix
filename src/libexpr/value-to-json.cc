#include "value-to-json.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "signals.hh"

#include <cstdlib>
#include <iomanip>
#include <nlohmann/json.hpp>


namespace nix {
using json = nlohmann::json;
json printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore)
{
    checkInterrupt();

    if (strict) state.forceValue(v, pos);

    json out;

    switch (v.type()) {

        case nInt:
            out = v.integer();
            break;

        case nBool:
            out = v.boolean();
            break;

        case nString:
            copyContext(v, context);
            out = v.c_str();
            break;

        case nPath:
            if (copyToStore)
                out = state.store->printStorePath(
                    state.copyPathToStore(context, v.path()));
            else
                out = v.path().path.abs();
            break;

        case nNull:
            // already initialized as null
            break;

        case nAttrs: {
            auto maybeString = state.tryAttrsToString(pos, v, context, false, false);
            if (maybeString) {
                out = *maybeString;
                break;
            }
            if (auto i = v.attrs()->get(state.sOutPath))
                return printValueAsJSON(state, strict, *i->value, i->pos, context, copyToStore);
            else {
                out = json::object();
                for (auto & a : v.attrs()->lexicographicOrder(state.symbols)) {
                    try {
                        out[state.symbols[a->name]] = printValueAsJSON(state, strict, *a->value, a->pos, context, copyToStore);
                    } catch (Error & e) {
                        e.addTrace(state.positions[a->pos],
                            HintFmt("while evaluating attribute '%1%'", state.symbols[a->name]));
                        throw;
                    }
                }
            }
            break;
        }

        case nList: {
            out = json::array();
            int i = 0;
            for (auto elem : v.listItems()) {
                try {
                    out.push_back(printValueAsJSON(state, strict, *elem, pos, context, copyToStore));
                } catch (Error & e) {
                    e.addTrace(state.positions[pos],
                        HintFmt("while evaluating list element at index %1%", i));
                    throw;
                }
                i++;
            }
            break;
        }

        case nExternal:
            return v.external()->printValueAsJSON(state, strict, context, copyToStore);
            break;

        case nFloat:
            out = v.fpoint();
            break;

        case nThunk:
        case nFunction:
            state.error<TypeError>(
                "cannot convert %1% to JSON",
                showType(v)
            )
            .atPos(v.determinePos(pos))
            .debugThrow();
    }
    return out;
}

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, NixStringContext & context, bool copyToStore)
{
    str << printValueAsJSON(state, strict, v, pos, context, copyToStore);
}

json ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
    NixStringContext & context, bool copyToStore) const
{
    state.error<TypeError>("cannot convert %1% to JSON", showType())
    .debugThrow();
}


}
