#include "store-dir-config.hh"
#include "config-parse-impl.hh"
#include "util.hh"
#include "globals.hh"

namespace nix {

static const StoreDirConfigT<config::SettingInfo> storeDirConfigDescriptions = {
    ._storeDir{
        .name = "store",
        .description = R"(
              Logical location of the Nix store, usually
              `/nix/store`. Note that you can only copy store paths
              between stores if they have the same `store` setting.
            )",
    },
};

#define STORE_DIR_CONFIG_FIELDS(X) X(_storeDir),

MAKE_PARSE(StoreDirConfig, storeDirConfig, STORE_DIR_CONFIG_FIELDS)

static StoreDirConfigT<config::JustValue> storeDirConfigDefaults()
{
    return {
        ._storeDir = {settings.nixStore},
    };
}

MAKE_APPLY_PARSE(StoreDirConfig, storeDirConfig, STORE_DIR_CONFIG_FIELDS)

StoreDirConfig::StoreDirConfig(const StoreReference::Params & params)
    : StoreDirConfigT<config::JustValue>{storeDirConfigApplyParse(params)}
    , MixStoreDirMethods{_storeDir.value}
{
}

config::SettingDescriptionMap StoreDirConfig::descriptions()
{
    constexpr auto & descriptions = storeDirConfigDescriptions;
    auto defaults = storeDirConfigDefaults();
    return {STORE_DIR_CONFIG_FIELDS(DESC_ROW)};
}

}
