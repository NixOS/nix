#include "print-ambiguous.hh"
#include "print.hh"
#include "signals.hh"
#include "eval.hh"

namespace nix {

// See: https://github.com/NixOS/nix/issues/9730
void printAmbiguous(
    Value &v,
    const SymbolTable &symbols,
    std::ostream &str,
    std::set<const void *> *seen,
    int depth)
{
    checkInterrupt();

    if (depth <= 0) {
        str << "«too deep»";
        return;
    }
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
            for (auto & i : v.attrs()->lexicographicOrder(symbols)) {
                str << symbols[i->name] << " = ";
                printAmbiguous(*i->value, symbols, str, seen, depth - 1);
                str << "; ";
            }
            str << "}";
        }
        break;
    }
    case nList:
        if (seen && v.listSize() && !seen->insert(v.listElems()).second)
            str << "«repeated»";
        else {
            str << "[ ";
            for (auto v2 : v.listItems()) {
                if (v2)
                    printAmbiguous(*v2, symbols, str, seen, depth - 1);
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
        abort();
    }
}

}
