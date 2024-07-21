#include "binary-cache-store.hh"

namespace nix {

struct HttpBinaryCacheStoreConfig : virtual BinaryCacheStoreConfig
{
    HttpBinaryCacheStoreConfig(
        std::string_view scheme, std::string_view cacheUri, const StoreReference::Params & params);

    Path cacheUri;

    const std::string name() override
    {
        return "HTTP Binary Cache Store";
    }

    static std::set<std::string> uriSchemes()
    {
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1";
        auto ret = std::set<std::string>({"http", "https"});
        if (forceHttp)
            ret.insert("file");
        return ret;
    }

    std::string doc() override;

    ref<Store> openStore() const override;
};

}
