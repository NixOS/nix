#include "nix/expr/print-ambiguous.hh"
#include "nix/expr/print.hh"
#include "nix/util/signals.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-error.hh"

namespace nix {

// See: https://github.com/NixOS/nix/issues/9730
void printAmbiguous(EvalState & state, Value & v, std::ostream & str, std::set<const void *> * seen, size_t depth)
{
    checkInterrupt();

    if (depth > state.settings.maxCallDepth)
        state.error<StackOverflowError>().atPos(v.determinePos(noPos)).debugThrow();
    switch (v.type()) {
    case nInt:
        str << v.integer();
        break;
    case nBool:
        printLiteralBool(str, v.boolean());
        break;
    case nString:
        printLiteralString(str, v.string_view());
        break;
    case nPath:
        str << v.path().to_string(); // !!! escaping?
        break;
    case nNull:
        str << "null";
        break;
    case nAttrs: {
        if (seen && !v.attrs()->empty() && !seen->insert(v.attrs()).second)
            str << "«repeated»";
        else {
            str << "{ ";
            for (auto & i : v.attrs()->lexicographicOrder(state.symbols)) {
                str << state.symbols[i->name] << " = ";
                printAmbiguous(state, *i->value, str, seen, depth + 1);
                str << "; ";
            }
            str << "}";
        }
        break;
    }
    case nList:
        /* Use pointer to the Value instead of pointer to the elements, because
           that would need to explicitly handle the case of SmallList. */
        if (seen && v.listSize() && !seen->insert(&v).second)
            str << "«repeated»";
        else {
            str << "[ ";
            for (auto v2 : v.listView()) {
                if (v2)
                    printAmbiguous(state, *v2, str, seen, depth + 1);
                else
                    str << "(nullptr)";
                str << " ";
            }
            str << "]";
        }
        break;
    case nThunk:
        if (!v.isBlackhole()) {
            str << "<CODE>";
        } else {
            // Although we know for sure that it's going to be an infinite recursion
            // when this value is accessed _in the current context_, it's likely
            // that the user will misinterpret a simpler «infinite recursion» output
            // as a definitive statement about the value, while in fact it may be
            // a valid value after `builtins.trace` and perhaps some other steps
            // have completed.
            str << "«potential infinite recursion»";
        }
        break;
    case nFailed:
        // Historically, a tried and then ignored value (e.g. through tryEval) was
        // reverted to the original thunk.
        str << "<CODE>";
        break;
    case nFunction:
        if (v.isLambda()) {
            str << "<LAMBDA>";
        } else if (v.isPrimOp()) {
            str << "<PRIMOP>";
        } else if (v.isPrimOpApp()) {
            str << "<PRIMOP-APP>";
        }
        break;
    case nExternal:
        str << *v.external();
        break;
    case nFloat:
        str << v.fpoint();
        break;
    default:
        printError("Nix evaluator internal error: printAmbiguous: invalid value type");
        unreachable();
    }
}

} // namespace nix
