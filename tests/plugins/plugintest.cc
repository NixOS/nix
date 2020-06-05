#include "config.hh"
#include "primops.hh"

using namespace nix;

struct MySettings : Config
{
    Setting<bool> settingSet{this, false, "setting-set",
        "Whether the plugin-defined setting was set"};
};

MySettings mySettings;

static GlobalConfig::Register rs(&mySettings);

static void prim_anotherNull (EvalState & state, const Pos & pos, Value ** args, Value & v)
{
    if (mySettings.settingSet)
        mkNull(v);
    else
        mkBool(v, false);
}

static RegisterPrimOp rp("anotherNull", 0, prim_anotherNull);
