#include "value.hh"
#include "eval.hh"

namespace nix {

const Value * getPrimOp(const Value &v) {
    const Value * primOp = &v;
    while (primOp->isPrimOpApp()) {
        primOp = primOp->primOpApp.left;
    }
    assert(primOp->isPrimOp());
    return primOp;
}

std::string_view showType(ValueType type, bool withArticle)
{
    #define WA(a, w) withArticle ? a " " w : w
    switch (type) {
        case nInt: return WA("an", "integer");
        case nBool: return WA("a", "Boolean");
        case nString: return WA("a", "string");
        case nPath: return WA("a", "path");
        case nNull: return "null";
        case nAttrs: return WA("a", "set");
        case nList: return WA("a", "list");
        case nFunction: return WA("a", "function");
        case nExternal: return WA("an", "external value");
        case nFloat: return WA("a", "float");
        case nThunk: return WA("a", "thunk");
    }
    abort();
}

std::string showType(const Value & v)
{
    // Allow selecting a subset of enum values
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (v.internalType) {
        case tString: return v.string.context ? "a string with context" : "a string";
        case tPrimOp:
            return fmt("the built-in function '%s'", std::string(v.primOp->name));
        case tPrimOpApp:
            return fmt("the partially applied built-in function '%s'", std::string(getPrimOp(v)->primOp->name));
        case tExternal: return v.external->showType();
        case tThunk: return v.isBlackhole() ? "a black hole" : "a thunk";
        case tApp: return "a function application";
    default:
        return std::string(showType(v.type()));
    }
    #pragma GCC diagnostic pop
}

}
