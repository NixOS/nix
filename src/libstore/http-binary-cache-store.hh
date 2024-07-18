#include "binary-cache-store.hh"

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

    std::string doc() override;
};

}
