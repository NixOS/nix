#include "binary-cache-store.hh"

namespace nix {

struct LocalBinaryCacheStoreConfig : std::enable_shared_from_this<LocalBinaryCacheStoreConfig>,
                                     Store::Config,
                                     BinaryCacheStoreConfig
{
    static config::SettingDescriptionMap descriptions();

    /**
     * @param binaryCacheDir `file://` is a short-hand for `file:///`
     * for now.
     */
    LocalBinaryCacheStoreConfig(
        std::string_view scheme, PathView binaryCacheDir, const StoreReference::Params & params);

    Path binaryCacheDir;

    static const std::string name()
    {
        return "Local Binary Cache Store";
    }

    static std::set<std::string> uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;
};

}
