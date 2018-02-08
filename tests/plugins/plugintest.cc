#include "primops.hh"

using namespace nix;

static void prim_constNull (EvalState & state, const Pos & pos, Value ** args, Value & v)
{
    mkNull(v);
}

static RegisterPrimOp r("constNull", 1, prim_constNull);
