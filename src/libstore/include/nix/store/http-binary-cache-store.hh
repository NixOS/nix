#include "nix/store/binary-cache-store.hh"

namespace nix {

struct HttpBinaryCacheStoreConfig : std::enable_shared_from_this<HttpBinaryCacheStoreConfig>,
                                    virtual Store::Config,
                                    BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    HttpBinaryCacheStoreConfig(
        std::string_view scheme, std::string_view cacheUri, const Store::Config::Params & params);

    Path cacheUri;

    const Setting<std::string> sslCert{
        this, "", "ssl-cert", "An optional SSL client certificate in PEM format; see CURLOPT_SSLCERT."};

    const Setting<std::string> sslKey{
        this, "", "ssl-key", "The SSL client certificate key in PEM format; see CURLOPT_SSLKEY."};

    static const std::string name()
    {
        return "HTTP Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;
};

}
