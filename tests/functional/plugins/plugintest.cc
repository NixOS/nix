#include "nix/util/config-global.hh"
#include "nix/expr/primops.hh"

using namespace nix;

struct MySettings : Config
{
    Setting<bool> settingSet{this, false, "setting-set", "Whether the plugin-defined setting was set"};
};

MySettings mySettings;

static GlobalConfig::Register rs(&mySettings);

static void prim_anotherNull(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    if (mySettings.settingSet)
        v.mkNull();
    else
        v.mkBool(false);
}

static RegisterPrimOp rp({
    .name = "anotherNull",
    .arity = 0,
    .impl = prim_anotherNull,
});
