#include "globals.hh"
#include "primops.hh"

using namespace nix;

static BaseSetting<bool> settingSet{false, "setting-set",
        "Whether the plugin-defined setting was set"};

static RegisterSetting rs(&settingSet);

static void prim_anotherNull (EvalState & state, const Pos & pos, Value ** args, Value & v)
{
    if (settingSet)
        mkNull(v);
    else
        mkBool(v, false);
}

static RegisterPrimOp rp("anotherNull", 0, prim_anotherNull);
