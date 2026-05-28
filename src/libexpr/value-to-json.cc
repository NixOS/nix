#include "nix/expr/value-to-json.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/store-api.hh"
#include "value-path.hh"
#include "nix/util/finally.hh"
#include "nix/util/signals.hh"

#include <cstdlib>
#include <boost/unordered/unordered_flat_map.hpp>
#include <nlohmann/json.hpp>

namespace nix {
using json = nlohmann::json;

MakeError(JSONCycleError, InfiniteRecursionError);

using SeenValuePaths = boost::unordered_flat_map<const void *, ValuePath>;

static json valueToJSON(
    EvalState & state,
    bool strict,
    Value & v,
    const PosIdx pos,
    NixStringContext & context,
    bool copyToStore,
    SeenValuePaths & seen,
    ValuePath * currentPath)
{
    checkInterrupt();

    auto _level = state.addCallDepth(pos);

    if (strict)
        state.forceValue(v, pos);

    json out;

    auto cycleError = [&](const void * key, const ValuePath & firstSeenAt) {
        if (currentPath)
            state
                .error<InfiniteRecursionError>(
                    "infinite recursion encountered while converting Nix value to JSON: %s is the same as %s (only cycle-free data can be represented in JSON)",
                    showValuePath(state.symbols, *currentPath),
                    showValuePath(state.symbols, firstSeenAt))
                .atPos(pos)
                .debugThrow();
        else
            // Internal signal caught by the public wrapper, which then re-runs
            // with path tracking. Bypasses EvalErrorBuilder so we don't need
            // an explicit template instantiation in eval-error.cc.
            throw JSONCycleError(
                state,
                "infinite recursion encountered while converting Nix value to JSON (only cycle-free data can be represented in JSON)");
    };

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
        auto maybeString = state.tryAttrsToString(pos, v, context, false, false);
        if (maybeString) {
            out = *maybeString;
            break;
        }
        const void * key = v.attrs();
        auto [it, fresh] = seen.try_emplace(key, currentPath ? *currentPath : ValuePath{});
        if (!fresh)
            cycleError(key, it->second);
        Finally cleanup([&] { seen.erase(key); });
        if (auto i = v.attrs()->get(state.s.outPath)) {
            if (currentPath)
                currentPath->emplace_back(state.s.outPath);
            Finally popOutPath([&] {
                if (currentPath)
                    currentPath->pop_back();
            });
            return valueToJSON(state, strict, *i->value, i->pos, context, copyToStore, seen, currentPath);
        } else {
            out = json::object();
            for (auto & a : v.attrs()->lexicographicOrder(state.symbols)) {
                if (currentPath)
                    currentPath->emplace_back(a->name);
                Finally popSegment([&] {
                    if (currentPath)
                        currentPath->pop_back();
                });
                try {
                    out.emplace(
                        state.symbols[a->name],
                        valueToJSON(state, strict, *a->value, a->pos, context, copyToStore, seen, currentPath));
                } catch (Error & e) {
                    e.addTrace(
                        state.positions[a->pos], HintFmt("while evaluating attribute '%1%'", state.symbols[a->name]));
                    throw;
                }
            }
        }
        break;
    }

    case nList: {
        const void * key = &v;
        auto [it, fresh] = seen.try_emplace(key, currentPath ? *currentPath : ValuePath{});
        if (!fresh)
            cycleError(key, it->second);
        Finally cleanup([&] { seen.erase(key); });
        out = json::array();
        size_t i = 0;
        for (auto elem : v.listView()) {
            if (currentPath)
                currentPath->emplace_back(i);
            Finally popSegment([&] {
                if (currentPath)
                    currentPath->pop_back();
            });
            try {
                out.push_back(valueToJSON(state, strict, *elem, pos, context, copyToStore, seen, currentPath));
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

// TODO: rename. It doesn't print.
json printValueAsJSON(
    EvalState & state, bool strict, Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore)
{
    SeenValuePaths seen;
    try {
        return valueToJSON(state, strict, v, pos, context, copyToStore, seen, nullptr);
    } catch (JSONCycleError &) {
        // The fast pass detected a cycle. Re-run with path tracking so we can
        // produce a richer error that names both ends of the cycle.
        seen.clear();
        ValuePath path;
        return valueToJSON(state, strict, v, pos, context, copyToStore, seen, &path);
    }
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
