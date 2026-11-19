#include "nix/util/config-global.hh"
#include "nix/expr/primops.hh"
#include "nix/store/local-store.hh"
#include "nix/store/store-registration.hh"

namespace nix {

struct MySettings : Config
{
    Setting<bool> settingSet{this, false, "setting-set", "Whether the plugin-defined setting was set"};
};

MySettings mySettings;

static GlobalConfig::Register rs(&mySettings);

static void prim_anotherNull(EvalState & state, CallSite callSite, Value * const * args, Value & v)
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

/* A store type that only this plugin provides: the local store under
   another scheme. */
static struct RegisterPluginTestStore
{
    RegisterPluginTestStore()
    {
        auto factory = Implementations::registered().at(LocalStore::Config::name());
        factory.uriSchemes = {"plugintest"};
        Implementations::registered().emplace("Plugin Test Store", std::move(factory));
    }
} registerPluginTestStore;

} // namespace nix
