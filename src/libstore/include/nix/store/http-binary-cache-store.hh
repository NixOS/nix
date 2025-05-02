#include "nix/store/binary-cache-store.hh"

namespace nix {

struct HttpBinaryCacheStoreConfig : virtual BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    HttpBinaryCacheStoreConfig(std::string_view scheme, std::string_view _cacheUri, const Params & params);

    Path cacheUri;

    const std::string name() override
    {
        return "HTTP Binary Cache Store";
    }

    static StringSet uriSchemes()
    {
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1";
        auto ret = StringSet({"http", "https"});
        if (forceHttp)
            ret.insert("file");
        return ret;
    }

    std::string doc() override;
};

}
