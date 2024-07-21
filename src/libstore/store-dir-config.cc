#include "store-dir-config.hh"
#include "config-parse-impl.hh"
#include "util.hh"

namespace nix {

const StoreDirConfigT<config::SettingInfo> StoreDirConfig::descriptions = {
    ._storeDir =
        {
            .name = "store",
            .description = R"(
              Logical location of the Nix store, usually
              `/nix/store`. Note that you can only copy store paths
              between stores if they have the same `store` setting.
            )",
        },
};

const StoreDirConfigT<config::JustValue> StoreDirConfig::defaults = {
    ._storeDir = {settings.nixStore},
};

StoreDirConfig::StoreDirConfig(const StoreReference::Params & params)
    : StoreDirConfigT<config::JustValue>{
        CONFIG_ROW(_storeDir),
    }
{
}

}
