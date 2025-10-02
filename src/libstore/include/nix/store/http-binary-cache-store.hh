#pragma once
///@file

#include "nix/util/url.hh"
#include "nix/store/binary-cache-store.hh"

namespace nix {

struct HttpBinaryCacheStoreConfig : std::enable_shared_from_this<HttpBinaryCacheStoreConfig>,
                                    virtual Store::Config,
                                    BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    HttpBinaryCacheStoreConfig(
        std::string_view scheme, std::string_view cacheUri, const Store::Config::Params & params);

    ParsedURL cacheUri;

    const Setting<std::string> narinfoCompression{
        this, "", "narinfo-compression", "Compression method for `.narinfo` files."};

    const Setting<std::string> lsCompression{this, "", "ls-compression", "Compression method for `.ls` files."};

    const Setting<std::string> logCompression{
        this,
        "",
        "log-compression",
        R"(
          Compression method for `log/*` files. It is recommended to
          use a compression method supported by most web browsers
          (e.g. `brotli`).
        )"};

    static const std::string name()
    {
        return "HTTP Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

} // namespace nix
