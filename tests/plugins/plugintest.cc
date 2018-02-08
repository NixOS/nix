#include "primops.hh"

using namespace nix;

static void prim_anotherNull (EvalState & state, const Pos & pos, Value ** args, Value & v)
{
    mkNull(v);
}

static RegisterPrimOp r("anotherNull", 0, prim_anotherNull);
