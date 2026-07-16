#include "nix/expr/value-to-json.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/store-api.hh"
#include "nix/util/signals.hh"
#include "nix/expr/parallel-eval.hh"

#include <cstdlib>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace nix {

using json = nlohmann::json;

// TODO: rename. It doesn't print.
json printValueAsJSON(
    EvalState & state, bool strict, Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore)
{
    if (strict && state.executor->enabled && !Executor::amWorkerThread)
        state.forceValueDeepParallel(v, pos);

    auto recurse = [&](this const auto & recurse, json & res, Value & v, PosIdx pos) -> void {
        checkInterrupt();

        auto _level = state.addCallDepth(pos);

        if (strict)
            state.forceValue(v, pos);

        switch (v.type()) {

        case nInt:
            res = v.integer().value;
            break;

        case nBool:
            res = v.boolean();
            break;

        case nString: {
            copyContext(v, context);
            res = v.string_view();
            break;
        }

        case nPath:
            if (copyToStore)
                res = state.store->printStorePath(state.copyPathToStore(context, v.path(), v.determinePos(pos)));
            else
                res = v.path().path.abs();
            break;

        case nNull:
            // already initialized as null
            break;

        case nAttrs: {
            auto maybeString = state.tryAttrsToString(pos, v, context, false, false);
            if (maybeString) {
                res = *maybeString;
                break;
            }
            if (auto i = v.attrs()->get(state.s.outPath))
                return recurse(res, *i->value, i->pos);
            else {
                res = json::object();
                for (auto & a : v.attrs()->lexicographicOrder(state.symbols)) {
                    json & j = res.emplace(state.symbols[a->name], json()).first.value();
                    try {
                        recurse(j, *a->value, a->pos);
                    } catch (Error & e) {
                        e.addTrace(
                            state.positions[a->pos],
                            HintFmt("while evaluating attribute '%1%'", state.symbols[a->name]));
                        throw;
                    }
                }
            }
            break;
        }

        case nList: {
            res = json::array();
            for (const auto & [i, elem] : enumerate(v.listView())) {
                try {
                    res.push_back(json());
                    recurse(res.back(), *elem, pos);
                } catch (Error & e) {
                    e.addTrace(state.positions[pos], HintFmt("while evaluating list element at index %1%", i));
                    throw;
                }
            }
            break;
        }

        case nExternal: {
            res = v.external()->printValueAsJSON(state, strict, context, copyToStore);
            break;
        }

        case nFloat:
            res = v.fpoint();
            break;

        case nThunk:
        case nFailed:
        case nFunction:
            state.error<TypeError>("cannot convert %1% to JSON", showType(v)).atPos(v.determinePos(pos)).debugThrow();
        }
    };

    json res;

    recurse(res, v, pos);

    return res;
}

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
