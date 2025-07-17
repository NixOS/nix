#include "nix/expr/value-to-json.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/store-api.hh"
#include "nix/util/signals.hh"

#include <cstdlib>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <typeinfo>


namespace nix {
using json = nlohmann::json;
// TODO: rename. It doesn't print.
json printValueAsJSON(EvalState & state, bool strict, bool replaceEvalErrors,
    Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore)
{
    checkInterrupt();

    if (strict) state.forceValue(v, pos);

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
                return printValueAsJSON(state, strict, replaceEvalErrors, *i->value, i->pos, context, copyToStore);
            else {
                out = json::object();
                for (auto & a : v.attrs()->lexicographicOrder(state.symbols)) {
                    try {
                        out.emplace(state.symbols[a->name], printValueAsJSON(state, strict, replaceEvalErrors, *a->value, a->pos, context, copyToStore));
                    } catch (Error & e) {
                        std::cerr << "Caught an Error of type: " << typeid(e).name() << std::endl;
                        // std::cerr << "Caught an Error of type: " << e.message() << std::endl;
                        // std::cerr << "Caught an Error of type: " << e.what() << std::endl;

                        // TODO: Figure out what Error is here?
                        // We seem to be not catching FileNotFoundError.
                        bool isEvalError = dynamic_cast<EvalError *>(&e);
                        bool isFileNotFoundError = dynamic_cast<FileNotFound *>(&e);
                        // Restrict replaceEvalErrors only only evaluation errors
                        if (replaceEvalErrors && (isEvalError || isFileNotFoundError)) {
                            out.emplace(state.symbols[a->name], "«evaluation error»");
                            continue;
                        }

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
            for (auto elem : v.listView()) {
                try {
                    out.push_back(printValueAsJSON(state, strict, replaceEvalErrors, *elem, pos, context, copyToStore));
                } catch (Error & e) {
                    // TODO: Missing catch
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

void printValueAsJSON(EvalState & state, bool strict, bool replaceEvalErrors,
    Value & v, const PosIdx pos, std::ostream & str, NixStringContext & context, bool copyToStore)
{
    try {
        str << printValueAsJSON(state, strict, replaceEvalErrors, v, pos, context, copyToStore);
    } catch (nlohmann::json::exception & e) {
        throw JSONSerializationError("JSON serialization error: %s", e.what());
    }
}

json ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
    NixStringContext & context, bool copyToStore) const
{
    state.error<TypeError>("cannot convert %1% to JSON", showType())
    .debugThrow();
}


}
