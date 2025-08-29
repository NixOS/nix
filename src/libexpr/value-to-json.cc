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
    EvalState & state, bool strict, Value & v, const PosIdx pos, NixStringContext & context_, bool copyToStore)
{
    FutureVector futures(*state.executor);

    auto doParallel = state.executor->enabled && !Executor::amWorkerThread;

    auto spawn = [&](auto work) {
        if (doParallel) {
            futures.spawn(0, [work{std::move(work)}]() { work(); });
        } else {
            work();
        }
    };

    struct State
    {
        NixStringContext & context;
    };

    Sync<State> state_{State{.context = context_}};

    auto addContext = [&](const NixStringContext & context) {
        auto state(state_.lock());
        for (auto & c : context)
            state->context.insert(c);
    };

    std::function<void(json & res, Value & v, PosIdx pos)> recurse;

    recurse = [&](json & res, Value & v, PosIdx pos) {
        checkInterrupt();

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
            NixStringContext context;
            copyContext(v, context);
            addContext(context);
            res = v.c_str();
            break;
        }

        case nPath:
            if (copyToStore) {
                NixStringContext context;
                res = state.store->printStorePath(state.copyPathToStore(context, v.path(), v.determinePos(pos)));
                addContext(context);
            } else
                res = v.path().path.abs();
            break;

        case nNull:
            // already initialized as null
            break;

        case nAttrs: {
            NixStringContext context;
            auto maybeString = state.tryAttrsToString(pos, v, context, false, false);
            addContext(context);
            if (maybeString) {
                res = *maybeString;
                break;
            }
            if (auto i = v.attrs()->get(state.sOutPath))
                return recurse(res, *i->value, i->pos);
            else {
                res = json::object();
                for (auto & a : v.attrs()->lexicographicOrder(state.symbols)) {
                    json & j = res.emplace(state.symbols[a->name], json()).first.value();
                    spawn([&, copyToStore, a]() {
                        try {
                            recurse(j, *a->value, a->pos);
                        } catch (Error & e) {
                            e.addTrace(
                                state.positions[a->pos],
                                HintFmt("while evaluating attribute '%1%'", state.symbols[a->name]));
                            throw;
                        }
                    });
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
            NixStringContext context;
            res = v.external()->printValueAsJSON(state, strict, context, copyToStore);
            addContext(context);
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

    futures.finishAll();

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
