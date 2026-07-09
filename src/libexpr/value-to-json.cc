#include "nix/expr/value-to-json.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/store-api.hh"
#include "nix/util/signals.hh"

#include <cstdlib>
#include <nlohmann/json.hpp>

namespace nix {
using json = nlohmann::json;

// TODO: rename. It doesn't print.
json printValueAsJSON(
    EvalState & state, bool strict, Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore)
{
    checkInterrupt();

    auto _level = state.addCallDepth(pos);

    if (strict)
        state.forceValue(v, pos);

    json out;

    switch (v.type()) {

    case nInt:
        out = v.integer().value;
        break;

    case nBool:
        out = v.boolean();
        break;

    case nString:
        copyContext(v, context);
        out = v.string_view();
        break;

    case nPath:
        if (copyToStore)
            out = state.store->printStorePath(state.copyPathToStore(context, v.path()));
        else
            out = v.path().path.abs();
        break;

    case nNull:
        // already initialized as null
        break;

    case nAttrs: {
        out = state.peelToStringOutPath(
            pos, v, /*checkToStringReturn=*/true, [&](Value * peeled, bool cameThroughToString) -> json {
                if (peeled->type() != nAttrs) {
                    // Historical quirk preserved here for reproducibility:
                    // In some coercions, Nix would coerce paths to a raw string
                    // if they came from a __toString result.
                    auto copyToStore2 = copyToStore && !cameThroughToString;
                    return printValueAsJSON(state, strict, *peeled, pos, context, copyToStore2);
                }
                // Peelable attrs handled. Returned attrs are not peelable.
                // Quirk: builtins.toJSON { outPath.foo = true; } == "{\"foo\":true}"
                // All that remains is to return a JSON object.
                json obj = json::object();
                for (auto & a : peeled->attrs()->lexicographicOrder(state.symbols)) {
                    try {
                        obj.emplace(
                            state.symbols[a->name],
                            printValueAsJSON(state, strict, *a->value, a->pos, context, copyToStore));
                    } catch (Error & e) {
                        e.addTrace(
                            state.positions[a->pos],
                            HintFmt("while evaluating attribute '%1%'", state.symbols[a->name]));
                        throw;
                    }
                }
                return obj;
            });
        break;
    }

    case nList: {
        out = json::array();
        int i = 0;
        for (auto elem : v.listView()) {
            try {
                out.push_back(printValueAsJSON(state, strict, *elem, pos, context, copyToStore));
            } catch (Error & e) {
                e.addTrace(state.positions[pos], HintFmt("while evaluating list element at index %1%", i));
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
    case nFailed:
    case nFunction:
        state.error<TypeError>("cannot convert %1% to JSON", showType(v)).atPos(v.determinePos(pos)).debugThrow();
    }
    return out;
}

void JSONSerializationError::anchor() {}

void printValueAsJSON(
    EvalState & state,
    bool strict,
    Value & v,
    const PosIdx pos,
    std::ostream & str,
    NixStringContext & context,
    bool copyToStore)
{
    try {
        str << printValueAsJSON(state, strict, v, pos, context, copyToStore);
    } catch (nlohmann::json::exception & e) {
        throw JSONSerializationError("JSON serialization error: %s", e.what());
    }
}

json ExternalValueBase::printValueAsJSON(
    EvalState & state, bool strict, NixStringContext & context, bool copyToStore) const
{
    state.error<TypeError>("cannot convert %1% to JSON", showType()).debugThrow();
}

} // namespace nix
