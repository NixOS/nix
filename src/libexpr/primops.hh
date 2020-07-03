#include "eval.hh"

#include <tuple>
#include <vector>

namespace nix {

struct RegisterPrimOp
{
    struct Info
    {
        std::string name;
        size_t arity;
        PrimOpFun primOp;
        std::optional<std::string> requiredFeature;
    };

    typedef std::vector<Info> PrimOps;
    static PrimOps * primOps;

    /* You can register a constant by passing an arity of 0. fun
       will get called during EvalState initialization, so there
       may be primops not yet added and builtins is not yet sorted. */
    RegisterPrimOp(
        std::string name,
        size_t arity,
        PrimOpFun fun,
        std::optional<std::string> requiredFeature = {});
};

/* These primops are disabled without enableNativeCode, but plugins
   may wish to use them in limited contexts without globally enabling
   them. */
/* Load a ValueInitializer from a DSO and return whatever it initializes */
void prim_importNative(EvalState & state, const Pos & pos, Value * * args, Value & v);

/* Execute a program and parse its output */
void prim_exec(EvalState & state, const Pos & pos, Value * * args, Value & v);

}
