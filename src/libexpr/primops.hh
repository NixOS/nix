#include "eval.hh"

#include <tuple>
#include <vector>

namespace nix {

struct RegisterPrimOp
{
    typedef std::vector<std::tuple<std::string, size_t, PrimOpFun>> PrimOps;
    static PrimOps * primOps;
    /* You can register a constant by passing an arity of 0. fun
       will get called during EvalState initialization, so there
       may be primops not yet added and builtins is not yet sorted. */
    RegisterPrimOp(std::string name, size_t arity, PrimOpFun fun);
};

}
