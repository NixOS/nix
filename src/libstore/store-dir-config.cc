#include "nix/store/store-dir-config.hh"
#include "nix/store/config-parse-impl.hh"
#include "nix/util/util.hh"
#include "nix/store/globals.hh"

namespace nix {

constexpr static const StoreDirConfigT<config::SettingInfo> storeDirConfigDescriptions = {
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

static StoreDirConfigT<config::PlainValue> storeDirConfigDefaults()
{
    return {
        ._storeDir = {settings.nixStore},
    };
}

MAKE_APPLY_PARSE(StoreDirConfig, storeDirConfig, STORE_DIR_CONFIG_FIELDS)

StoreDirConfig::StoreDirConfig(const StoreReference::Params & params)
    : StoreDirConfigT<config::PlainValue>{storeDirConfigApplyParse(params)}
    , MixStoreDirMethods{_storeDir.value}
{
}

config::SettingDescriptionMap StoreDirConfig::descriptions()
{
    constexpr auto & descriptions = storeDirConfigDescriptions;
    auto defaults = storeDirConfigDefaults();
    return {STORE_DIR_CONFIG_FIELDS(DESCRIBE_ROW)};
}

}
