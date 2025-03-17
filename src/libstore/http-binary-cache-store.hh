#include "binary-cache-store.hh"

namespace nix {

struct HttpBinaryCacheStoreConfig : std::enable_shared_from_this<HttpBinaryCacheStoreConfig>,
                                    Store::Config,
                                    BinaryCacheStoreConfig
{
    static config::SettingDescriptionMap descriptions();

    HttpBinaryCacheStoreConfig(
        std::string_view scheme, std::string_view cacheUri, const StoreReference::Params & params);

    Path cacheUri;

    static const std::string name()
    {
        return "HTTP Binary Cache Store";
    }

    static std::set<std::string> uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;
};

}
