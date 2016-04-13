#include "eval.hh"

#include <tuple>
#include <vector>

namespace nix {

struct RegisterPrimOp
{
    typedef std::vector<std::tuple<std::string, size_t, PrimOpFun>> PrimOps;
    static PrimOps * primOps;
    RegisterPrimOp(std::string name, size_t arity, PrimOpFun fun);
};

}
